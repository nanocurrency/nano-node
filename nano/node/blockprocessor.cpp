#include <nano/lib/threading.hpp>
#include <nano/lib/timer.hpp>
#include <nano/node/blockprocessor.hpp>
#include <nano/node/node.hpp>
#include <nano/secure/store.hpp>

#include <boost/format.hpp>

std::chrono::milliseconds constexpr nano::block_processor::confirmation_request_delay;

nano::block_processor::block_processor (nano::node & node_a, nano::write_database_queue & write_database_queue_a) :
	next_log (std::chrono::steady_clock::now ()),
	node (node_a),
	write_database_queue (write_database_queue_a),
	state_block_signature_verification (node.checker, node.ledger.constants.epochs, node.config, node.logger, node.flags.block_processor_verification_size)
{
	batch_processed.add ([this] (auto const & items) {
		// For every batch item: notify the 'processed' observer.
		for (auto const & item : items)
		{
			auto const & [result, block] = item;
			processed.notify (result, block);
		}
	});
	blocking.connect (*this);
	state_block_signature_verification.blocks_verified_callback = [this] (std::deque<nano::state_block_signature_verification::value_type> & items, std::vector<int> const & verifications, std::vector<nano::block_hash> const & hashes, std::vector<nano::signature> const & blocks_signatures) {
		this->process_verified_state_blocks (items, verifications, hashes, blocks_signatures);
	};
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
	blocking.stop ();
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

void nano::block_processor::add (std::shared_ptr<nano::block> const & block)
{
	if (full ())
	{
		node.stats.inc (nano::stat::type::blockprocessor, nano::stat::detail::overfill);
		return;
	}
	if (node.network_params.work.validate_entry (*block)) // true => error
	{
		node.stats.inc (nano::stat::type::blockprocessor, nano::stat::detail::insufficient_work);
		return;
	}
	add_impl (block);
	return;
}

std::optional<nano::process_return> nano::block_processor::add_blocking (std::shared_ptr<nano::block> const & block)
{
	auto future = blocking.insert (block);
	add_impl (block);
	condition.notify_all ();
	std::optional<nano::process_return> result;
	try
	{
		result = future.get ();
	}
	catch (std::future_error const &)
	{
	}
	return result;
}

void nano::block_processor::force (std::shared_ptr<nano::block> const & block_a)
{
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		forced.push_back (block_a);
	}
	condition.notify_all ();
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
			auto processed = process_batch (lock);
			batch_processed.notify (processed);
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

void nano::block_processor::process_verified_state_blocks (std::deque<nano::state_block_signature_verification::value_type> & items, std::vector<int> const & verifications, std::vector<nano::block_hash> const & hashes, std::vector<nano::signature> const & blocks_signatures)
{
	{
		nano::unique_lock<nano::mutex> lk{ mutex };
		for (auto i (0); i < verifications.size (); ++i)
		{
			debug_assert (verifications[i] == 1 || verifications[i] == 0);
			auto & item = items.front ();
			auto & [block] = item;
			if (!block->link ().is_zero () && node.ledger.is_epoch_link (block->link ()))
			{
				// Epoch blocks
				if (verifications[i] == 1)
				{
					blocks.emplace_back (block);
				}
				else
				{
					// Possible regular state blocks with epoch link (send subtype)
					blocks.emplace_back (block);
				}
			}
			else if (verifications[i] == 1)
			{
				// Non epoch blocks
				blocks.emplace_back (block);
			}
			items.pop_front ();
		}
	}
	condition.notify_all ();
}

void nano::block_processor::add_impl (std::shared_ptr<nano::block> block)
{
	if (block->type () == nano::block_type::state || block->type () == nano::block_type::open)
	{
		state_block_signature_verification.add ({ block });
	}
	else
	{
		{
			nano::lock_guard<nano::mutex> guard{ mutex };
			blocks.emplace_back (block);
		}
		condition.notify_all ();
	}
}

auto nano::block_processor::process_batch (nano::unique_lock<nano::mutex> & lock_a) -> std::deque<processed_t>
{
	std::deque<processed_t> processed;
	auto scoped_write_guard = write_database_queue.wait (nano::writer::process_batch);
	auto transaction (node.store.tx_begin_write ({ tables::accounts, tables::blocks, tables::frontiers, tables::pending, tables::unchecked }));
	nano::timer<std::chrono::milliseconds> timer_l;
	lock_a.lock ();
	timer_l.start ();
	// Processing blocks
	unsigned number_of_blocks_processed (0), number_of_forced_processed (0);
	auto deadline_reached = [&timer_l, deadline = node.config.block_processor_batch_max_time] { return timer_l.after_deadline (deadline); };
	auto processor_batch_reached = [&number_of_blocks_processed, max = node.flags.block_processor_batch_size] { return number_of_blocks_processed >= max; };
	auto store_batch_reached = [&number_of_blocks_processed, max = node.store.max_block_write_batch_num ()] { return number_of_blocks_processed >= max; };
	while (have_blocks_ready () && (!deadline_reached () || !processor_batch_reached ()) && !store_batch_reached ())
	{
		if ((blocks.size () + state_block_signature_verification.size () + forced.size () > 64) && should_log ())
		{
			node.logger.always_log (boost::str (boost::format ("%1% blocks (+ %2% state blocks) (+ %3% forced) in processing queue") % blocks.size () % state_block_signature_verification.size () % forced.size ()));
		}
		std::shared_ptr<nano::block> block;
		nano::block_hash hash (0);
		bool force (false);
		if (forced.empty ())
		{
			block = blocks.front ();
			blocks.pop_front ();
			hash = block->hash ();
		}
		else
		{
			block = forced.front ();
			forced.pop_front ();
			hash = block->hash ();
			force = true;
			number_of_forced_processed++;
		}
		lock_a.unlock ();
		if (force)
		{
			auto successor = node.ledger.successor (transaction, block->qualified_root ());
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
		auto result = process_one (transaction, block, force);
		processed.emplace_back (result, block);
		lock_a.lock ();
	}
	lock_a.unlock ();

	if (node.config.logging.timing_logging () && number_of_blocks_processed != 0 && timer_l.stop () > std::chrono::milliseconds (100))
	{
		node.logger.always_log (boost::str (boost::format ("Processed %1% blocks (%2% blocks were forced) in %3% %4%") % number_of_blocks_processed % number_of_forced_processed % timer_l.value ().count () % timer_l.unit ()));
	}
	return processed;
}

nano::process_return nano::block_processor::process_one (nano::write_transaction const & transaction_a, std::shared_ptr<nano::block> block, bool const forced_a)
{
	nano::process_return result;
	auto hash (block->hash ());
	result = node.ledger.process (transaction_a, *block);
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
			node.unchecked.put (block->previous (), block);
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
			node.stats.inc (nano::stat::type::ledger, nano::stat::detail::gap_source);
			break;
		}
		case nano::process_result::gap_epoch_open_pending:
		{
			if (node.config.logging.ledger_logging ())
			{
				node.logger.try_log (boost::str (boost::format ("Gap pending entries for epoch open: %1%") % hash.to_string ()));
			}
			node.unchecked.put (block->account (), block); // Specific unchecked key starting with epoch open block account public key
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

void nano::block_processor::queue_unchecked (nano::write_transaction const & transaction_a, nano::hash_or_account const & hash_or_account_a)
{
	node.unchecked.trigger (hash_or_account_a);
	node.gap_cache.erase (hash_or_account_a.hash);
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
