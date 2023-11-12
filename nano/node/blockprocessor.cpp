#include <nano/lib/threading.hpp>
#include <nano/lib/timer.hpp>
#include <nano/node/blockprocessor.hpp>
#include <nano/node/node.hpp>
#include <nano/store/component.hpp>

#include <boost/format.hpp>

#include <magic_enum.hpp>

/*
 * block_processor::context
 */

nano::block_processor::context::context (nano::block_processor::block_source source_a) :
	source{ source_a }
{
	debug_assert (source != nano::block_processor::block_source::unknown);
}

auto nano::block_processor::context::get_future () -> std::future<result_t>
{
	return promise.get_future ();
}

void nano::block_processor::context::set_result (result_t const & result)
{
	promise.set_value (result);
}

/*
 * block_processor
 */

nano::block_processor::block_processor (nano::node & node_a, nano::write_database_queue & write_database_queue_a) :
	node (node_a),
	write_database_queue (write_database_queue_a),
	next_log (std::chrono::steady_clock::now ())
{
	batch_processed.add ([this] (auto const & items) {
		// For every batch item: notify the 'processed' observer.
		for (auto const & [result, block, context] : items)
		{
			processed.notify (result, block, context);
		}
	});
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
	nano::join_or_pass (processing_thread);
}

std::size_t nano::block_processor::size ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	return blocks.size () + forced.size ();
}

bool nano::block_processor::full ()
{
	return size () >= node.flags.block_processor_full_size;
}

bool nano::block_processor::half_full ()
{
	return size () >= node.flags.block_processor_full_size / 2;
}

void nano::block_processor::add (std::shared_ptr<nano::block> const & block, block_source const source)
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
	add_impl (block, context{ source });
}

std::optional<nano::process_return> nano::block_processor::add_blocking (std::shared_ptr<nano::block> const & block, block_source const source)
{
	context ctx{ source };
	auto future = ctx.get_future ();
	add_impl (block, std::move (ctx));
	try
	{
		auto status = future.wait_for (node.config.block_process_timeout);
		debug_assert (status != std::future_status::deferred);
		if (status == std::future_status::ready)
		{
			return future.get ();
		}
	}
	catch (std::future_error const &)
	{
	}
	return std::nullopt;
}

void nano::block_processor::rollback_competitor (store::write_transaction const & transaction, nano::block const & block)
{
	auto hash = block.hash ();
	auto successor = node.ledger.successor (transaction, block.qualified_root ());
	if (successor != nullptr && successor->hash () != hash)
	{
		// Replace our block with the winner and roll back any dependent blocks
		node.logger.debug (nano::log::type::blockprocessor, "Rolling back: {} and replacing with: {}", successor->hash ().to_string (), hash.to_string ());

		std::vector<std::shared_ptr<nano::block>> rollback_list;
		if (node.ledger.rollback (transaction, successor->hash (), rollback_list))
		{
			node.stats.inc (nano::stat::type::ledger, nano::stat::detail::rollback_failed);
			node.logger.error (nano::log::type::blockprocessor, "Failed to roll back: {} because it or a successor was confirmed", successor->hash ().to_string ());
		}
		else
		{
			node.logger.debug (nano::log::type::blockprocessor, "Blocks rolled back: {}", rollback_list.size ());
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

void nano::block_processor::force (std::shared_ptr<nano::block> const & block_a)
{
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		forced.emplace_back (entry{ block_a, context{ block_source::forced } });
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
			debug_assert (!lock.owns_lock ());

			// Set results for futures when not holding the lock
			for (auto & [result, block, context] : processed)
			{
				context.set_result (result);
			}

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
		next_log = now + std::chrono::seconds (15);
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
	return have_blocks_ready ();
}

void nano::block_processor::add_impl (std::shared_ptr<nano::block> block, context ctx)
{
	release_assert (ctx.source != nano::block_processor::block_source::forced);
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		blocks.emplace_back (entry{ block, std::move (ctx) });
	}
	condition.notify_all ();
}

auto nano::block_processor::next () -> entry
{
	debug_assert (!mutex.try_lock ());
	debug_assert (!blocks.empty () || !forced.empty ()); // This should be checked before calling next

	if (!blocks.empty ())
	{
		entry entry = std::move (blocks.front ());
		release_assert (entry.ctx.source != nano::block_processor::block_source::forced);
		blocks.pop_front ();
		return entry;
	}

	if (!forced.empty ())
	{
		entry entry = std::move (forced.front ());
		release_assert (entry.ctx.source == nano::block_processor::block_source::forced);
		forced.pop_front ();
		return entry;
	}

	release_assert (false, "next() called when no blocks are ready");
}

auto nano::block_processor::process_batch (nano::unique_lock<nano::mutex> & lock_a) -> processed_batch_t
{
	processed_batch_t processed;

	auto scoped_write_guard = write_database_queue.wait (nano::writer::process_batch);
	auto transaction (node.store.tx_begin_write ({ tables::accounts, tables::blocks, tables::frontiers, tables::pending }));
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
		// TODO: Cleaner periodical logging
		if ((blocks.size () + forced.size () > 64) && should_log ())
		{
			node.logger.debug (nano::log::type::blockprocessor, "{} blocks (+ {} forced) in processing queue", blocks.size (), forced.size ());
		}

		entry entry = next ();
		context ctx = std::move (entry.ctx);
		auto const block = entry.block;
		auto const hash = block->hash ();
		bool const force = ctx.source == nano::block_processor::block_source::forced;

		lock_a.unlock ();

		if (force)
		{
			number_of_forced_processed++;
			rollback_competitor (transaction, *block);
		}

		number_of_blocks_processed++;

		auto result = process_one (transaction, block, ctx, force);
		processed.emplace_back (result, block, std::move (ctx));

		lock_a.lock ();
	}

	lock_a.unlock ();

	if (number_of_blocks_processed != 0 && timer_l.stop () > std::chrono::milliseconds (100))
	{
		node.logger.debug (nano::log::type::blockprocessor, "Processed {} blocks ({} forced) in {} {}", number_of_blocks_processed, number_of_forced_processed, timer_l.value ().count (), timer_l.unit ());
	}

	return processed;
}

nano::process_return nano::block_processor::process_one (store::write_transaction const & transaction_a, std::shared_ptr<nano::block> block, context const & context, bool const forced_a)
{
	auto const hash = block->hash ();
	nano::process_return result = node.ledger.process (transaction_a, *block);

	node.stats.inc (nano::stat::type::blockprocessor, to_stat_detail (result.code));
	node.stats.inc (nano::stat::type::blockprocessor_sources, to_stat_detail (context.source));
	node.logger.trace (nano::log::type::blockprocessor, nano::log::detail::block_processed,
	nano::log::arg{ "result", result.code },
	nano::log::arg{ "source", context.source },
	nano::log::arg{ "arrival", nano::log::microseconds (context.arrival) },
	nano::log::arg{ "forced", forced_a },
	nano::log::arg{ "block", block });

	switch (result.code)
	{
		case nano::process_result::progress:
		{
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
			node.unchecked.put (block->previous (), block);
			node.stats.inc (nano::stat::type::ledger, nano::stat::detail::gap_previous);
			break;
		}
		case nano::process_result::gap_source:
		{
			node.unchecked.put (node.ledger.block_source (transaction_a, *block), block);
			node.stats.inc (nano::stat::type::ledger, nano::stat::detail::gap_source);
			break;
		}
		case nano::process_result::gap_epoch_open_pending:
		{
			node.unchecked.put (block->account (), block); // Specific unchecked key starting with epoch open block account public key
			node.stats.inc (nano::stat::type::ledger, nano::stat::detail::gap_source);
			break;
		}
		case nano::process_result::old:
		{
			node.stats.inc (nano::stat::type::ledger, nano::stat::detail::old);
			break;
		}
		case nano::process_result::bad_signature:
		{
			break;
		}
		case nano::process_result::negative_spend:
		{
			break;
		}
		case nano::process_result::unreceivable:
		{
			break;
		}
		case nano::process_result::fork:
		{
			node.stats.inc (nano::stat::type::ledger, nano::stat::detail::fork);
			break;
		}
		case nano::process_result::opened_burn_account:
		{
			break;
		}
		case nano::process_result::balance_mismatch:
		{
			break;
		}
		case nano::process_result::representative_mismatch:
		{
			break;
		}
		case nano::process_result::block_position:
		{
			break;
		}
		case nano::process_result::insufficient_work:
		{
			break;
		}
	}
	return result;
}

void nano::block_processor::queue_unchecked (store::write_transaction const & transaction_a, nano::hash_or_account const & hash_or_account_a)
{
	node.unchecked.trigger (hash_or_account_a);
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
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "blocks", blocks_count, sizeof (decltype (block_processor.blocks)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "forced", forced_count, sizeof (decltype (block_processor.forced)::value_type) }));
	return composite;
}

nano::stat::detail nano::to_stat_detail (block_processor::block_source type)
{
	auto value = magic_enum::enum_cast<nano::stat::detail> (magic_enum::enum_name (type));
	debug_assert (value);
	return value.value_or (nano::stat::detail{});
}
