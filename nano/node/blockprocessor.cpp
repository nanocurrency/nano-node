#include <nano/lib/stats_enums.hpp>
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
	account_state{ node.ledger },
	epoch_restrictions{ node.ledger },
	link{ node.ledger.constants.epochs },
	next_log (std::chrono::steady_clock::now ()),
	node (node_a),
	write_database_queue (write_database_queue_a),
	state_block_signature_verification (node.checker, node.ledger.constants.epochs, node.config, node.logger, node.flags.block_processor_verification_size)
{
	// Pipeline begin
	pipeline = [this] (block_pipeline::context & context) {
		reserved.sink (context);
	};
	reserved.pass = [this] (block_pipeline::context & context) {
		node.stats.inc (nano::stat::type::block_pipeline, nano::stat::detail::reserved_account_filter_pass);
		account_state.sink (context);
	};
	reserved.reject = [this] (block_pipeline::context & context) {
		node.stats.inc (nano::stat::type::block_pipeline, nano::stat::detail::reserved_account_filter_reject);
	};
	account_state.pass = [this] (block_pipeline::context & context) {
		node.stats.inc (nano::stat::type::block_pipeline, nano::stat::detail::account_state_filter_pass);
		position.sink (context);
	};
	account_state.reject_existing = [this] (block_pipeline::context & context) {
		node.stats.inc (nano::stat::type::block_pipeline, nano::stat::detail::account_state_filter_reject_existing);
	};
	account_state.reject_gap = [this] (block_pipeline::context & context) {
		node.stats.inc (nano::stat::type::block_pipeline, nano::stat::detail::account_state_filter_reject_gap);
		node.gap_cache.add (context.block->hash ());
		node.unchecked.put (context.block->previous (), context.block);
	};
	position.pass = [this] (block_pipeline::context & context) {
		node.stats.inc (nano::stat::type::block_pipeline, nano::stat::detail::block_position_filter_pass);
		state_block_signature_verification.add (context);
	};
	position.reject = [this] (block_pipeline::context & context) {
		node.stats.inc (nano::stat::type::block_pipeline, nano::stat::detail::block_position_filter_reject);
	};
	state_block_signature_verification.blocks_verified_callback = [this] (std::deque<nano::state_block_signature_verification::value_type> & items, std::vector<int> const & verifications, std::vector<nano::block_hash> const & hashes, std::vector<nano::signature> const & blocks_signatures) {
		debug_assert (items.size () == verifications.size ());
		debug_assert (items.size () == hashes.size ());
		debug_assert (items.size () == blocks_signatures.size ());
		for (auto i = 0; i < items.size (); ++i)
		{
			release_assert (verifications[i] == 0 || verifications[i] == 1);
			if (verifications[i] == 1)
			{
				metastable.sink (items[i]);
			}
			else
			{
				std::cerr << "Signature failure\n";
			}
		}
	};
	metastable.pass = [this] (block_pipeline::context & context) {
		node.stats.inc (nano::stat::type::block_pipeline, nano::stat::detail::metastable_filter_pass);
		link.sink (context);
	};
	metastable.reject = [this] (block_pipeline::context & context) {
		node.stats.inc (nano::stat::type::block_pipeline, nano::stat::detail::metastable_filter_reject);
		this->node.active.publish (context.block);
	};
	link.hash = [this] (block_pipeline::context & context) {
		node.stats.inc (nano::stat::type::block_pipeline, nano::stat::detail::link_filter_hash);
		receive_restrictions.sink (context);
	};
	link.account = [this] (block_pipeline::context & context) {
		node.stats.inc (nano::stat::type::block_pipeline, nano::stat::detail::link_filter_account);
		send_restrictions.sink (context);
	};
	link.noop = [this] (block_pipeline::context & context) {
		node.stats.inc (nano::stat::type::block_pipeline, nano::stat::detail::link_filter_noop);
		enqueue (context);
	};
	link.epoch = [this] (block_pipeline::context & context) {
		node.stats.inc (nano::stat::type::block_pipeline, nano::stat::detail::link_filter_epoch);
		epoch_restrictions.sink (context);
	};
	epoch_restrictions.pass = [this] (block_pipeline::context & context) {
		node.stats.inc (nano::stat::type::block_pipeline, nano::stat::detail::epoch_restrictions_pass);
		enqueue (context);
	};
	epoch_restrictions.reject_balance = [this] (block_pipeline::context & context) {
		node.stats.inc (nano::stat::type::block_pipeline, nano::stat::detail::epoch_restrictions_reject_balance);
	};
	epoch_restrictions.reject_representative = [this] (block_pipeline::context & context) {
		node.stats.inc (nano::stat::type::block_pipeline, nano::stat::detail::epoch_restrictions_reject_representative);
	};
	epoch_restrictions.reject_gap_open = [this] (block_pipeline::context & context) {
		node.stats.inc (nano::stat::type::block_pipeline, nano::stat::detail::epoch_restrictions_reject_gap_open);
		node.unchecked.put (context.account (), context.block);
	};
	receive_restrictions.pass = [this] (block_pipeline::context & context) {
		node.stats.inc (nano::stat::type::block_pipeline, nano::stat::detail::receive_restrictions_filter_pass);
		enqueue (context);
	};
	receive_restrictions.reject_balance = [this] (block_pipeline::context & context) {
		node.stats.inc (nano::stat::type::block_pipeline, nano::stat::detail::receive_restrictions_filter_reject_balance);
	};
	receive_restrictions.reject_pending = [this] (block_pipeline::context & context) {
		node.stats.inc (nano::stat::type::block_pipeline, nano::stat::detail::receive_restrictions_filter_reject_pending);
		node.gap_cache.add (context.block->hash ());
		node.unchecked.put (context.source (), context.block);
	};
	send_restrictions.pass = [this] (block_pipeline::context & context) {
		node.stats.inc (nano::stat::type::block_pipeline, nano::stat::detail::send_restrictions_filter_pass);
		enqueue (context);
	};
	send_restrictions.reject = [this] (block_pipeline::context & context) {
		node.stats.inc (nano::stat::type::block_pipeline, nano::stat::detail::send_restrictions_filter_reject);
	};
	// Pipeline end
	state_block_signature_verification.transition_inactive_callback = [this] () {
		if (this->flushing)
		{
			{
				// Prevent a race with condition.wait in block_processor::flush
				nano::lock_guard<nano::mutex> guard{ this->mutex };
			}
			this->condition.notify_all ();
		}
	};
	processing_thread = std::thread ([this] () {
		nano::thread_role::set (nano::thread_role::name::block_processing);
		this->process_blocks ();
	});
}

void nano::block_processor::stop ()
{
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	state_block_signature_verification.stop ();
	nano::join_or_pass (processing_thread);
}

void nano::block_processor::flush ()
{
	node.checker.flush ();
	flushing = true;
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped && (have_blocks () || active || state_block_signature_verification.is_active ()))
	{
		condition.wait (lock);
	}
	flushing = false;
}

std::size_t nano::block_processor::size ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
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

void nano::block_processor::add (value_type & item)
{
	debug_assert (!node.network_params.work.validate_entry (*item.block));
	pipeline (item);
}

void nano::block_processor::add (std::shared_ptr<nano::block> const & block_a)
{
	value_type item{ block_a };
	add (item);
}

void nano::block_processor::force (std::shared_ptr<nano::block> const & block_a)
{
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		forced.push_back (block_a);
	}
	condition.notify_all ();
}

void nano::block_processor::wait_write ()
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	awaiting_write = true;
}

void nano::block_processor::process_blocks ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
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

void nano::block_processor::pipeline_dump ()
{
	auto dump_stage = [this] (nano::stat::detail detail) {
		std::cerr << boost::str (boost::format ("%1%: %2%\n") % nano::to_string (detail) % std::to_string (node.stats.count (nano::stat::type::block_pipeline, detail)));
	};
	dump_stage (nano::stat::detail::account_state_filter_pass);
	dump_stage (nano::stat::detail::account_state_filter_reject_existing);
	dump_stage (nano::stat::detail::account_state_filter_reject_gap);
	dump_stage (nano::stat::detail::block_position_filter_pass);
	dump_stage (nano::stat::detail::block_position_filter_reject);
	dump_stage (nano::stat::detail::link_filter_hash);
	dump_stage (nano::stat::detail::link_filter_account);
	dump_stage (nano::stat::detail::link_filter_noop);
	dump_stage (nano::stat::detail::link_filter_epoch);
	dump_stage (nano::stat::detail::metastable_filter_pass);
	dump_stage (nano::stat::detail::metastable_filter_reject);
	dump_stage (nano::stat::detail::receive_restrictions_filter_pass);
	dump_stage (nano::stat::detail::receive_restrictions_filter_reject_balance);
	dump_stage (nano::stat::detail::receive_restrictions_filter_reject_pending);
	dump_stage (nano::stat::detail::reserved_account_filter_pass);
	dump_stage (nano::stat::detail::reserved_account_filter_reject);
	dump_stage (nano::stat::detail::send_restrictions_filter_pass);
	dump_stage (nano::stat::detail::send_restrictions_filter_reject);
	std::cerr << '\n';
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
		value_type info;
		auto & block = info.block;
		nano::block_hash hash (0);
		bool force (false);
		if (forced.empty ())
		{
			info = blocks.front ();
			blocks.pop_front ();
			hash = block->hash ();
		}
		else
		{
			info = { forced.front () };
			forced.pop_front ();
			hash = block->hash ();
			force = true;
			number_of_forced_processed++;
		}
		lock_a.unlock ();
		if (force)
		{
			auto successor (node.ledger.successor (transaction, block->qualified_root ()));
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
					node.stats.inc (nano::stat::type::ledger, nano::stat::detail::rollback_failed);
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
		auto result = process_one (transaction, post_events, info, force);
		switch (result.code)
		{
			case nano::process_result::progress:
			case nano::process_result::old:
			case nano::process_result::fork:
				break;
			default:
				break;
		}
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

	// Notify inactive vote cache about a new live block
	node.inactive_vote_cache.trigger (block_a->hash ());

	// Announce block contents to the network
	if (origin_a == nano::block_origin::local)
	{
		node.network.flood_block_initial (block_a);
	}
	else if (!node.flags.disable_block_processor_republishing && node.block_arrival.recent (hash_a))
	{
		node.network.flood_block (block_a, nano::buffer_drop_policy::limiter);
	}

	if (node.websocket.server && node.websocket.server->any_subscriber (nano::websocket::topic::new_unconfirmed_block))
	{
		node.websocket.server->broadcast (nano::websocket::message_builder ().new_block_arrived (*block_a));
	}
}

nano::process_return nano::block_processor::process_one (nano::write_transaction const & transaction_a, block_post_events & events_a, value_type const & info_a, bool const forced_a, nano::block_origin const origin_a)
{
	nano::process_return result;
	auto block (info_a.block);
	auto hash (block->hash ());
	result = node.ledger.process (transaction_a, *block);
	events_a.events.emplace_back ([this, result, block = info_a.block] (nano::transaction const & tx) {
		processed.notify (tx, result, *block);
	});
	switch (result.code)
	{
		case nano::process_result::progress:
		{
			if (node.config.logging.ledger_logging ())
			{
				std::string block_string;
				block->serialize_json (block_string, node.config.logging.single_line_record ());
				node.logger.try_log (boost::str (boost::format ("Processing block %1%: %2%") % hash.to_string () % block_string));
			}
			events_a.events.emplace_back ([this, hash, block = info_a.block, result, origin_a] (nano::transaction const & post_event_transaction_a) {
				process_live (post_event_transaction_a, hash, block, result, origin_a);
			});
			queue_unchecked (hash);
			/* For send blocks check epoch open unchecked (gap pending).
			For state blocks check only send subtype and only if block epoch is not last epoch.
			If epoch is last, then pending entry shouldn't trigger same epoch open block for destination account. */
			if (block->type () == nano::block_type::send || (block->type () == nano::block_type::state && block->sideband ().details.is_send && std::underlying_type_t<nano::epoch> (block->sideband ().details.epoch) < std::underlying_type_t<nano::epoch> (nano::epoch::max)))
			{
				/* block->destination () for legacy send blocks
				block->link () for state blocks (send subtype) */
				queue_unchecked (block->destination ().is_zero () ? block->link () : block->destination ());
			}
			break;
		}
		case nano::process_result::gap_previous:
		{
			if (node.config.logging.ledger_logging ())
			{
				node.logger.try_log (boost::str (boost::format ("Gap previous for: %1%") % hash.to_string ()));
			}

			node.unchecked.put (block->previous (), nano::unchecked_info{ block });
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

			node.unchecked.put (node.ledger.block_source (transaction_a, *block), block);
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

			node.unchecked.put (block->account () /* Specific unchecked key starting with epoch open block account public key*/, block);
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
			events_a.events.emplace_back ([this, block = block] (nano::transaction const &) { this->node.active.publish (block); });
			if (node.config.logging.ledger_logging ())
			{
				node.logger.try_log (boost::str (boost::format ("Fork for: %1% root: %2%") % hash.to_string () % block->root ().to_string ()));
			}
			break;
		}
		case nano::process_result::opened_burn_account:
		{
			if (node.config.logging.ledger_logging ())
			{
				node.logger.try_log (boost::str (boost::format ("Rejecting open block for burn account: %1%") % hash.to_string ()));
			}
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

	node.stats.inc (nano::stat::type::blockprocessor, nano::to_stat_detail (result.code));

	return result;
}

nano::process_return nano::block_processor::process_one (nano::write_transaction const & transaction_a, block_post_events & events_a, std::shared_ptr<nano::block> const & block_a)
{
	auto result (process_one (transaction_a, events_a, { block_a }));
	return result;
}

void nano::block_processor::enqueue (value_type const & item)
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	blocks.emplace_back (item);
	lock.unlock ();
	condition.notify_all ();
}

void nano::block_processor::queue_unchecked (nano::hash_or_account const & hash_or_account_a)
{
	node.unchecked.trigger (hash_or_account_a);
	node.gap_cache.erase (hash_or_account_a.hash);
}

void nano::block_processor::requeue_invalid (nano::block_hash const & hash_a, value_type const & item)
{
	auto const & block = item.block;
	debug_assert (hash_a == block->hash ());
	node.bootstrap_initiator.lazy_requeue (hash_a, block->previous ());
}

std::unique_ptr<nano::container_info_component> nano::collect_container_info (block_processor & block_processor, std::string const & name)
{
	std::size_t blocks_count;
	std::size_t forced_count;

	{
		nano::lock_guard<nano::mutex> guard{ block_processor.mutex };
		blocks_count = block_processor.blocks.size ();
		forced_count = block_processor.forced.size ();
	}

	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (collect_container_info (block_processor.state_block_signature_verification, "state_block_signature_verification"));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "blocks", blocks_count, sizeof (decltype (block_processor.blocks)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "forced", forced_count, sizeof (decltype (block_processor.forced)::value_type) }));
	return composite;
}
