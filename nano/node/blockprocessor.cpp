#include <nano/lib/threading.hpp>
#include <nano/lib/timer.hpp>
#include <nano/node/blockprocessor.hpp>
#include <nano/node/election.hpp>
#include <nano/node/node.hpp>
#include <nano/node/websocket.hpp>
#include <nano/secure/store.hpp>

#include <boost/format.hpp>

std::chrono::milliseconds constexpr nano::block_processor::confirmation_request_delay;

nano::block_post_events::block_post_events (std::function<nano::read_transaction ()> && get_transaction_a) :
	get_transaction (std::move (get_transaction_a))
{
}

nano::block_post_events::~block_post_events ()
{
	debug_assert (get_transaction != nullptr);
	auto transaction (get_transaction ());
	for (auto const & i : events)
	{
		i (transaction);
	}
}

nano::block_processor::block_processor (nano::node & node_a, nano::write_database_queue & write_database_queue_a) :
	next_log (std::chrono::steady_clock::now ()),
	node (node_a),
	write_database_queue (write_database_queue_a),
	state_block_signature_verification (node.checker, node.ledger.constants.epochs, node.config, node.logger, node.flags.block_processor_verification_size)
{
	state_block_signature_verification.blocks_verified_callback = [this] (std::deque<nano::unchecked_info> & items, std::vector<int> const & verifications, std::vector<nano::block_hash> const & hashes, std::vector<nano::signature> const & blocks_signatures) {
		this->process_verified_state_blocks (items, verifications, hashes, blocks_signatures);
	};
	state_block_signature_verification.transition_inactive_callback = [this] () {
		if (this->flushing)
		{
			{
				// Prevent a race with condition.wait in block_processor::flush
				nano::lock_guard<nano::mutex> guard (this->mutex);
			}
			this->condition.notify_all ();
		}
	};
	processing_thread = std::thread ([this] () {
		nano::thread_role::set (nano::thread_role::name::block_processing);
		this->process_blocks ();
	});
}

nano::block_processor::~block_processor ()
{
	stop ();
	if (processing_thread.joinable ())
	{
		processing_thread.join ();
	}
}

void nano::block_processor::stop ()
{
	{
		nano::lock_guard<nano::mutex> lock (mutex);
		stopped = true;
	}
	condition.notify_all ();
	state_block_signature_verification.stop ();
}

void nano::block_processor::flush ()
{
	node.checker.flush ();
	flushing = true;
	nano::unique_lock<nano::mutex> lock (mutex);
	while (!stopped && (have_blocks () || active || state_block_signature_verification.is_active ()))
	{
		condition.wait (lock);
	}
	flushing = false;
}

std::size_t nano::block_processor::size ()
{
	nano::unique_lock<nano::mutex> lock (mutex);
	return (blocks.size () + state_block_signature_verification.size () + forced.size ());
}

bool nano::block_processor::full ()
{
	return size () >= node.flags.block_processor_full_size;
}

bool nano::block_processor::half_full ()
{
	return size () >= node.flags.block_processor_full_size / 2;
}

void nano::block_processor::add (std::shared_ptr<nano::block> const & block_a, uint64_t origination)
{
	nano::unchecked_info info (block_a, 0, origination, nano::signature_verification::unknown);
	add (info);
}

void nano::block_processor::add (nano::unchecked_info const & info_a)
{
	debug_assert (!node.network_params.work.validate_entry (*info_a.block));
	bool quarter_full (size () > node.flags.block_processor_full_size / 4);
	if (info_a.verified == nano::signature_verification::unknown && (info_a.block->type () == nano::block_type::state || info_a.block->type () == nano::block_type::open || !info_a.account.is_zero ()))
	{
		state_block_signature_verification.add (info_a);
	}
	else
	{
		{
			nano::lock_guard<nano::mutex> guard (mutex);
			blocks.emplace_back (info_a);
		}
		condition.notify_all ();
	}
}

void nano::block_processor::add_local (nano::unchecked_info const & info_a)
{
	release_assert (info_a.verified == nano::signature_verification::unknown && (info_a.block->type () == nano::block_type::state || !info_a.account.is_zero ()));
	debug_assert (!node.network_params.work.validate_entry (*info_a.block));
	state_block_signature_verification.add (info_a);
}

void nano::block_processor::force (std::shared_ptr<nano::block> const & block_a)
{
	{
		nano::lock_guard<nano::mutex> lock (mutex);
		forced.push_back (block_a);
	}
	condition.notify_all ();
}

void nano::block_processor::wait_write ()
{
	nano::lock_guard<nano::mutex> lock (mutex);
	awaiting_write = true;
}

void nano::block_processor::process_blocks ()
{
	nano::unique_lock<nano::mutex> lock (mutex);
	while (!stopped)
	{
		if (have_blocks_ready ())
		{
			active = true;
			lock.unlock ();
			process_batch (lock);
			lock.lock ();
			active = false;
		}
		else
		{
			condition.notify_one ();
			condition.wait (lock);
		}
	}
}

bool nano::block_processor::should_log ()
{
	auto result (false);
	auto now (std::chrono::steady_clock::now ());
	if (next_log < now)
	{
		next_log = now + (node.config.logging.timing_logging () ? std::chrono::seconds (2) : std::chrono::seconds (15));
		result = true;
	}
	return result;
}

bool nano::block_processor::have_blocks_ready ()
{
	debug_assert (!mutex.try_lock ());
	return !blocks.empty () || !forced.empty ();
}

bool nano::block_processor::have_blocks ()
{
	debug_assert (!mutex.try_lock ());
	return have_blocks_ready () || state_block_signature_verification.size () != 0;
}

void nano::block_processor::process_verified_state_blocks (std::deque<nano::unchecked_info> & items, std::vector<int> const & verifications, std::vector<nano::block_hash> const & hashes, std::vector<nano::signature> const & blocks_signatures)
{
	{
		nano::unique_lock<nano::mutex> lk (mutex);
		for (auto i (0); i < verifications.size (); ++i)
		{
			debug_assert (verifications[i] == 1 || verifications[i] == 0);
			auto & item = items.front ();
			if (!item.block->link ().is_zero () && node.ledger.is_epoch_link (item.block->link ()))
			{
				// Epoch blocks
				if (verifications[i] == 1)
				{
					item.verified = nano::signature_verification::valid_epoch;
					blocks.emplace_back (std::move (item));
				}
				else
				{
					// Possible regular state blocks with epoch link (send subtype)
					item.verified = nano::signature_verification::unknown;
					blocks.emplace_back (std::move (item));
				}
			}
			else if (verifications[i] == 1)
			{
				// Non epoch blocks
				item.verified = nano::signature_verification::valid;
				blocks.emplace_back (std::move (item));
			}
			else
			{
				requeue_invalid (hashes[i], item);
			}
			items.pop_front ();
		}
	}
	condition.notify_all ();
}

void nano::block_processor::process_batch (nano::unique_lock<nano::mutex> & lock_a)
{
	auto scoped_write_guard = write_database_queue.wait (nano::writer::process_batch);
	block_post_events post_events ([&store = node.store] { return store.tx_begin_read (); });
	auto transaction (node.store.tx_begin_write ({ tables::accounts, tables::blocks, tables::frontiers, tables::pending, tables::unchecked }));
	nano::timer<std::chrono::milliseconds> timer_l;
	lock_a.lock ();
	timer_l.start ();
	// Processing blocks
	unsigned number_of_blocks_processed (0), number_of_forced_processed (0);
	auto deadline_reached = [&timer_l, deadline = node.config.block_processor_batch_max_time] { return timer_l.after_deadline (deadline); };
	auto processor_batch_reached = [&number_of_blocks_processed, max = node.flags.block_processor_batch_size] { return number_of_blocks_processed >= max; };
	auto store_batch_reached = [&number_of_blocks_processed, max = node.store.max_block_write_batch_num ()] { return number_of_blocks_processed >= max; };
	while (have_blocks_ready () && (!deadline_reached () || !processor_batch_reached ()) && !awaiting_write && !store_batch_reached ())
	{
		if ((blocks.size () + state_block_signature_verification.size () + forced.size () > 64) && should_log ())
		{
			node.logger.always_log (boost::str (boost::format ("%1% blocks (+ %2% state blocks) (+ %3% forced) in processing queue") % blocks.size () % state_block_signature_verification.size () % forced.size ()));
		}
		nano::unchecked_info info;
		nano::block_hash hash (0);
		bool force (false);
		if (forced.empty ())
		{
			info = blocks.front ();
			blocks.pop_front ();
			hash = info.block->hash ();
		}
		else
		{
			info = nano::unchecked_info (forced.front (), 0, nano::seconds_since_epoch (), nano::signature_verification::unknown);
			forced.pop_front ();
			hash = info.block->hash ();
			force = true;
			number_of_forced_processed++;
		}
		lock_a.unlock ();
		if (force)
		{
			auto successor (node.ledger.successor (transaction, info.block->qualified_root ()));
			if (successor != nullptr && successor->hash () != hash)
			{
				// Replace our block with the winner and roll back any dependent blocks
				if (node.config.logging.ledger_rollback_logging ())
				{
					node.logger.always_log (boost::str (boost::format ("Rolling back %1% and replacing with %2%") % successor->hash ().to_string () % hash.to_string ()));
				}
				std::vector<std::shared_ptr<nano::block>> rollback_list;
				if (node.ledger.rollback (transaction, successor->hash (), rollback_list))
				{
					node.logger.always_log (nano::severity_level::error, boost::str (boost::format ("Failed to roll back %1% because it or a successor was confirmed") % successor->hash ().to_string ()));
				}
				else if (node.config.logging.ledger_rollback_logging ())
				{
					node.logger.always_log (boost::str (boost::format ("%1% blocks rolled back") % rollback_list.size ()));
				}
				// Deleting from votes cache, stop active transaction
				for (auto & i : rollback_list)
				{
					node.history.erase (i->root ());
					// Stop all rolled back active transactions except initial
					if (i->hash () != successor->hash ())
					{
						node.active.erase (*i);
					}
				}
			}
		}
		number_of_blocks_processed++;
		process_one (transaction, post_events, info, force);
		lock_a.lock ();
	}
	awaiting_write = false;
	lock_a.unlock ();

	if (node.config.logging.timing_logging () && number_of_blocks_processed != 0 && timer_l.stop () > std::chrono::milliseconds (100))
	{
		node.logger.always_log (boost::str (boost::format ("Processed %1% blocks (%2% blocks were forced) in %3% %4%") % number_of_blocks_processed % number_of_forced_processed % timer_l.value ().count () % timer_l.unit ()));
	}
}

void nano::block_processor::process_live (nano::transaction const & transaction_a, nano::block_hash const & hash_a, std::shared_ptr<nano::block> const & block_a, nano::process_return const & process_return_a, nano::block_origin const origin_a)
{
	// Start collecting quorum on block
	if (node.ledger.dependents_confirmed (transaction_a, *block_a))
	{
		auto account = block_a->account ().is_zero () ? block_a->sideband ().account : block_a->account ();
		node.scheduler.activate (account, transaction_a);
	}
	else
	{
		node.active.trigger_inactive_votes_cache_election (block_a);
	}

	// Announce block contents to the network
	if (origin_a == nano::block_origin::local)
	{
		node.network.flood_block_initial (block_a);
	}
	else if (!node.flags.disable_block_processor_republishing)
	{
		node.network.flood_block (block_a, nano::buffer_drop_policy::limiter);
	}

	if (node.websocket_server && node.websocket_server->any_subscriber (nano::websocket::topic::new_unconfirmed_block))
	{
		node.websocket_server->broadcast (nano::websocket::message_builder ().new_block_arrived (*block_a));
	}
}

nano::process_return nano::block_processor::process_one (nano::write_transaction const & transaction_a, block_post_events & events_a, nano::unchecked_info info_a, bool const forced_a, nano::block_origin const origin_a)
{
	nano::process_return result;
	auto block (info_a.block);
	auto hash (block->hash ());
	result = node.ledger.process (transaction_a, *block, info_a.verified);
	switch (result.code)
	{
		case nano::process_result::progress:
		{
			release_assert (info_a.account.is_zero () || info_a.account == node.store.block.account_calculated (*block));
			if (node.config.logging.ledger_logging ())
			{
				std::string block_string;
				block->serialize_json (block_string, node.config.logging.single_line_record ());
				node.logger.try_log (boost::str (boost::format ("Processing block %1%: %2%") % hash.to_string () % block_string));
			}
			if ((info_a.modified > nano::seconds_since_epoch () - 300 && node.block_arrival.recent (hash)) || forced_a)
			{
				events_a.events.emplace_back ([this, hash, block = info_a.block, result, origin_a] (nano::transaction const & post_event_transaction_a) { process_live (post_event_transaction_a, hash, block, result, origin_a); });
			}
			queue_unchecked (transaction_a, hash);
			/* For send blocks check epoch open unchecked (gap pending).
			For state blocks check only send subtype and only if block epoch is not last epoch.
			If epoch is last, then pending entry shouldn't trigger same epoch open block for destination account. */
			if (block->type () == nano::block_type::send || (block->type () == nano::block_type::state && block->sideband ().details.is_send && std::underlying_type_t<nano::epoch> (block->sideband ().details.epoch) < std::underlying_type_t<nano::epoch> (nano::epoch::max)))
			{
				/* block->destination () for legacy send blocks
				block->link () for state blocks (send subtype) */
				queue_unchecked (transaction_a, block->destination ().is_zero () ? block->link () : block->destination ());
			}
			break;
		}
		case nano::process_result::gap_previous:
		{
			if (node.config.logging.ledger_logging ())
			{
				node.logger.try_log (boost::str (boost::format ("Gap previous for: %1%") % hash.to_string ()));
			}
			info_a.verified = result.verified;
			if (info_a.modified == 0)
			{
				info_a.modified = nano::seconds_since_epoch ();
			}

			nano::unchecked_key unchecked_key (block->previous (), hash);
			node.store.unchecked.put (transaction_a, unchecked_key, info_a);

			events_a.events.emplace_back ([this, hash] (nano::transaction const & /* unused */) { this->node.gap_cache.add (hash); });

			node.stats.inc (nano::stat::type::ledger, nano::stat::detail::gap_previous);
			break;
		}
		case nano::process_result::gap_source:
		{
			if (node.config.logging.ledger_logging ())
			{
				node.logger.try_log (boost::str (boost::format ("Gap source for: %1%") % hash.to_string ()));
			}
			info_a.verified = result.verified;
			if (info_a.modified == 0)
			{
				info_a.modified = nano::seconds_since_epoch ();
			}

			nano::unchecked_key unchecked_key (node.ledger.block_source (transaction_a, *(block)), hash);
			node.store.unchecked.put (transaction_a, unchecked_key, info_a);

			events_a.events.emplace_back ([this, hash] (nano::transaction const & /* unused */) { this->node.gap_cache.add (hash); });

			node.stats.inc (nano::stat::type::ledger, nano::stat::detail::gap_source);
			break;
		}
		case nano::process_result::gap_epoch_open_pending:
		{
			if (node.config.logging.ledger_logging ())
			{
				node.logger.try_log (boost::str (boost::format ("Gap pending entries for epoch open: %1%") % hash.to_string ()));
			}
			info_a.verified = result.verified;
			if (info_a.modified == 0)
			{
				info_a.modified = nano::seconds_since_epoch ();
			}

			nano::unchecked_key unchecked_key (block->account (), hash); // Specific unchecked key starting with epoch open block account public key
			node.store.unchecked.put (transaction_a, unchecked_key, info_a);

			node.stats.inc (nano::stat::type::ledger, nano::stat::detail::gap_source);
			break;
		}
		case nano::process_result::old:
		{
			if (node.config.logging.ledger_duplicate_logging ())
			{
				node.logger.try_log (boost::str (boost::format ("Old for: %1%") % hash.to_string ()));
			}
			node.stats.inc (nano::stat::type::ledger, nano::stat::detail::old);
			break;
		}
		case nano::process_result::bad_signature:
		{
			if (node.config.logging.ledger_logging ())
			{
				node.logger.try_log (boost::str (boost::format ("Bad signature for: %1%") % hash.to_string ()));
			}
			events_a.events.emplace_back ([this, hash, info_a] (nano::transaction const & /* unused */) { requeue_invalid (hash, info_a); });
			break;
		}
		case nano::process_result::negative_spend:
		{
			if (node.config.logging.ledger_logging ())
			{
				node.logger.try_log (boost::str (boost::format ("Negative spend for: %1%") % hash.to_string ()));
			}
			break;
		}
		case nano::process_result::unreceivable:
		{
			if (node.config.logging.ledger_logging ())
			{
				node.logger.try_log (boost::str (boost::format ("Unreceivable for: %1%") % hash.to_string ()));
			}
			break;
		}
		case nano::process_result::fork:
		{
			node.stats.inc (nano::stat::type::ledger, nano::stat::detail::fork);
			events_a.events.emplace_back ([this, block] (nano::transaction const &) { this->node.active.publish (block); });
			if (node.config.logging.ledger_logging ())
			{
				node.logger.try_log (boost::str (boost::format ("Fork for: %1% root: %2%") % hash.to_string () % block->root ().to_string ()));
			}
			break;
		}
		case nano::process_result::opened_burn_account:
		{
			node.logger.always_log (boost::str (boost::format ("*** Rejecting open block for burn account ***: %1%") % hash.to_string ()));
			break;
		}
		case nano::process_result::balance_mismatch:
		{
			if (node.config.logging.ledger_logging ())
			{
				node.logger.try_log (boost::str (boost::format ("Balance mismatch for: %1%") % hash.to_string ()));
			}
			break;
		}
		case nano::process_result::representative_mismatch:
		{
			if (node.config.logging.ledger_logging ())
			{
				node.logger.try_log (boost::str (boost::format ("Representative mismatch for: %1%") % hash.to_string ()));
			}
			break;
		}
		case nano::process_result::block_position:
		{
			if (node.config.logging.ledger_logging ())
			{
				node.logger.try_log (boost::str (boost::format ("Block %1% cannot follow predecessor %2%") % hash.to_string () % block->previous ().to_string ()));
			}
			break;
		}
		case nano::process_result::insufficient_work:
		{
			if (node.config.logging.ledger_logging ())
			{
				node.logger.try_log (boost::str (boost::format ("Insufficient work for %1% : %2% (difficulty %3%)") % hash.to_string () % nano::to_string_hex (block->block_work ()) % nano::to_string_hex (node.network_params.work.difficulty (*block))));
			}
			break;
		}
	}
	return result;
}

nano::process_return nano::block_processor::process_one (nano::write_transaction const & transaction_a, block_post_events & events_a, std::shared_ptr<nano::block> const & block_a)
{
	nano::unchecked_info info (block_a, block_a->account (), 0, nano::signature_verification::unknown);
	auto result (process_one (transaction_a, events_a, info));
	return result;
}

void nano::block_processor::queue_unchecked (nano::write_transaction const & transaction_a, nano::hash_or_account const & hash_or_account_a)
{
	auto unchecked_blocks (node.store.unchecked.get (transaction_a, hash_or_account_a.hash));
	for (auto & info : unchecked_blocks)
	{
		if (!node.flags.disable_block_processor_unchecked_deletion)
		{
			node.store.unchecked.del (transaction_a, nano::unchecked_key (hash_or_account_a, info.block->hash ()));
		}
		add (info);
	}
	node.gap_cache.erase (hash_or_account_a.hash);
}

void nano::block_processor::requeue_invalid (nano::block_hash const & hash_a, nano::unchecked_info const & info_a)
{
	debug_assert (hash_a == info_a.block->hash ());
	node.bootstrap_initiator.lazy_requeue (hash_a, info_a.block->previous (), info_a.confirmed);
}

std::unique_ptr<nano::container_info_component> nano::collect_container_info (block_processor & block_processor, std::string const & name)
{
	std::size_t blocks_count;
	std::size_t forced_count;

	{
		nano::lock_guard<nano::mutex> guard (block_processor.mutex);
		blocks_count = block_processor.blocks.size ();
		forced_count = block_processor.forced.size ();
	}

	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (collect_container_info (block_processor.state_block_signature_verification, "state_block_signature_verification"));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "blocks", blocks_count, sizeof (decltype (block_processor.blocks)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "forced", forced_count, sizeof (decltype (block_processor.forced)::value_type) }));
	return composite;
}
