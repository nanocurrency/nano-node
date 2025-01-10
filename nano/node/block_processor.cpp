#include <celerix/lib/block_type.hpp>
#include <celerix/lib/blocks.hpp>
#include <celerix/lib/enum_util.hpp>
#include <celerix/lib/threading.hpp>
#include <celerix/lib/timer.hpp>
#include <celerix/node/active_elections.hpp>
#include <celerix/node/block_processor.hpp>
#include <celerix/node/local_vote_history.hpp>
#include <celerix/node/node.hpp>
#include <celerix/node/unchecked_map.hpp>
#include <celerix/secure/ledger.hpp>
#include <celerix/secure/ledger_set_any.hpp>
#include <celerix/store/component.hpp>

#include <utility>

/*
 * block_processor
 */

celerix::block_processor::block_processor (celerix::node_config const & node_config, celerix::ledger & ledger_a, celerix::unchecked_map & unchecked_a, celerix::stats & stats_a, celerix::logger & logger_a) :
	config{ node_config.block_processor },
	network_params{ node_config.network_params },
	ledger{ ledger_a },
	unchecked{ unchecked_a },
	stats{ stats_a },
	logger{ logger_a },
	workers{ 1, celerix::thread_role::name::block_processing_notifications }
{
	queue.max_size_query = [this] (auto const & origin) {
		switch (origin.source)
		{
			case celerix::block_source::live:
			case celerix::block_source::live_originator:
				return config.max_peer_queue;
			default:
				return config.max_system_queue;
		}
	};

	queue.priority_query = [this] (auto const & origin) -> size_t {
		switch (origin.source)
		{
			case celerix::block_source::live:
			case celerix::block_source::live_originator:
				return config.priority_live;
			case celerix::block_source::bootstrap:
			case celerix::block_source::bootstrap_legacy:
			case celerix::block_source::unchecked:
				return config.priority_bootstrap;
			case celerix::block_source::local:
				return config.priority_local;
			default:
				return config.priority_system;
		}
	};

	// Requeue blocks that could not be immediately processed
	unchecked.satisfied.add ([this] (celerix::unchecked_info const & info) {
		add (info.block, celerix::block_source::unchecked);
	});
}

celerix::block_processor::~block_processor ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
	debug_assert (!workers.alive ());
}

void celerix::block_processor::start ()
{
	debug_assert (!thread.joinable ());

	workers.start ();

	thread = std::thread ([this] () {
		celerix::thread_role::set (celerix::thread_role::name::block_processing);
		run ();
	});
}

void celerix::block_processor::stop ()
{
	{
		celerix::lock_guard<celerix::mutex> lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	if (thread.joinable ())
	{
		thread.join ();
	}
	workers.stop ();
}

// TODO: Remove and replace all checks with calls to size (block_source)
std::size_t celerix::block_processor::size () const
{
	celerix::unique_lock<celerix::mutex> lock{ mutex };
	return queue.size ();
}

std::size_t celerix::block_processor::size (celerix::block_source source) const
{
	celerix::unique_lock<celerix::mutex> lock{ mutex };
	return queue.size ({ source });
}

bool celerix::block_processor::add (std::shared_ptr<celerix::block> const & block, block_source const source, std::shared_ptr<celerix::transport::channel> const & channel, std::function<void (celerix::block_status)> callback)
{
	if (network_params.work.validate_entry (*block)) // true => error
	{
		stats.inc (celerix::stat::type::block_processor, celerix::stat::detail::insufficient_work);
		return false; // Not added
	}

	stats.inc (celerix::stat::type::block_processor, celerix::stat::detail::process);
	logger.debug (celerix::log::type::block_processor, "Processing block (async): {} (source: {} {})",
	block->hash ().to_string (),
	to_string (source),
	channel ? channel->to_string () : "<unknown>"); // TODO: Lazy eval

	return add_impl (context{ block, source, std::move (callback) }, channel);
}

std::optional<celerix::block_status> celerix::block_processor::add_blocking (std::shared_ptr<celerix::block> const & block, block_source const source)
{
	stats.inc (celerix::stat::type::block_processor, celerix::stat::detail::process_blocking);
	logger.debug (celerix::log::type::block_processor, "Processing block (blocking): {} (source: {})", block->hash ().to_string (), to_string (source));

	context ctx{ block, source };
	auto future = ctx.get_future ();
	add_impl (std::move (ctx));

	try
	{
		future.wait ();
		return future.get ();
	}
	catch (std::future_error const &)
	{
		stats.inc (celerix::stat::type::block_processor, celerix::stat::detail::process_blocking_timeout);
		logger.error (celerix::log::type::block_processor, "Block dropped when processing: {}", block->hash ().to_string ());
	}

	return std::nullopt;
}

void celerix::block_processor::force (std::shared_ptr<celerix::block> const & block_a)
{
	stats.inc (celerix::stat::type::block_processor, celerix::stat::detail::force);
	logger.debug (celerix::log::type::block_processor, "Forcing block: {}", block_a->hash ().to_string ());

	add_impl (context{ block_a, block_source::forced });
}

bool celerix::block_processor::add_impl (context ctx, std::shared_ptr<celerix::transport::channel> const & channel)
{
	auto const source = ctx.source;
	bool added = false;
	{
		celerix::lock_guard<celerix::mutex> guard{ mutex };
		added = queue.push (std::move (ctx), { source, channel });
	}
	if (added)
	{
		condition.notify_all ();
	}
	else
	{
		stats.inc (celerix::stat::type::block_processor, celerix::stat::detail::overfill);
		stats.inc (celerix::stat::type::block_processor_overfill, to_stat_detail (source));
	}
	return added;
}

void celerix::block_processor::rollback_competitor (secure::write_transaction const & transaction, celerix::block const & fork_block)
{
	auto const hash = fork_block.hash ();
	auto const successor_hash = ledger.any.block_successor (transaction, fork_block.qualified_root ());
	auto const successor = successor_hash ? ledger.any.block_get (transaction, successor_hash.value ()) : nullptr;
	if (successor != nullptr && successor->hash () != hash)
	{
		// Replace our block with the winner and roll back any dependent blocks
		logger.debug (celerix::log::type::block_processor, "Rolling back: {} and replacing with: {}", successor->hash ().to_string (), hash.to_string ());

		std::deque<std::shared_ptr<celerix::block>> rollback_list;
		if (ledger.rollback (transaction, successor->hash (), rollback_list))
		{
			stats.inc (celerix::stat::type::ledger, celerix::stat::detail::rollback_failed);
			logger.error (celerix::log::type::block_processor, "Failed to roll back: {} because it or a successor was confirmed", successor->hash ().to_string ());
		}
		else
		{
			stats.inc (celerix::stat::type::ledger, celerix::stat::detail::rollback);
			logger.debug (celerix::log::type::block_processor, "Blocks rolled back: {}", rollback_list.size ());
		}

		// Notify observers of the rolled back blocks on a background thread while not holding the ledger write lock
		workers.post ([this, rollback_list = std::move (rollback_list), root = fork_block.qualified_root ()] () {
			rolled_back.notify (rollback_list, root);
		});
	}
}

void celerix::block_processor::run ()
{
	celerix::interval log_interval;
	celerix::unique_lock<celerix::mutex> lock{ mutex };
	while (!stopped)
	{
		if (!queue.empty ())
		{
			// It's possible that ledger processing happens faster than the notifications can be processed by other components, cooldown here
			while (workers.queued_tasks () >= config.max_queued_notifications)
			{
				stats.inc (celerix::stat::type::block_processor, celerix::stat::detail::cooldown);
				condition.wait_for (lock, 100ms, [this] { return stopped; });
				if (stopped)
				{
					return;
				}
			}

			if (log_interval.elapsed (15s))
			{
				logger.info (celerix::log::type::block_processor, "{} blocks (+ {} forced) in processing queue",
				queue.size (),
				queue.size ({ celerix::block_source::forced }));
			}

			auto processed = process_batch (lock);
			debug_assert (!lock.owns_lock ());
			lock.lock ();

			// Queue notifications to be dispatched in the background
			workers.post ([this, processed = std::move (processed)] () mutable {
				stats.inc (celerix::stat::type::block_processor, celerix::stat::detail::notify);
				// Set results for futures when not holding the lock
				for (auto & [result, context] : processed)
				{
					if (context.callback)
					{
						context.callback (result);
					}
					context.set_result (result);
				}
				batch_processed.notify (processed);
			});
		}
		else
		{
			condition.wait (lock, [this] {
				return stopped || !queue.empty ();
			});
		}
	}
}

auto celerix::block_processor::next () -> context
{
	debug_assert (!mutex.try_lock ());
	debug_assert (!queue.empty ()); // This should be checked before calling next

	if (!queue.empty ())
	{
		auto [request, origin] = queue.next ();
		release_assert (origin.source != celerix::block_source::forced || request.source == celerix::block_source::forced);
		return std::move (request);
	}

	release_assert (false, "next() called when no blocks are ready");
}

auto celerix::block_processor::next_batch (size_t max_count) -> std::deque<context>
{
	debug_assert (!mutex.try_lock ());
	debug_assert (!queue.empty ());

	queue.periodic_update ();

	std::deque<context> results;
	while (!queue.empty () && results.size () < max_count)
	{
		results.push_back (next ());
	}
	return results;
}

auto celerix::block_processor::process_batch (celerix::unique_lock<celerix::mutex> & lock) -> processed_batch_t
{
	debug_assert (lock.owns_lock ());
	debug_assert (!mutex.try_lock ());
	debug_assert (!queue.empty ());

	auto batch = next_batch (config.batch_size);

	lock.unlock ();

	auto transaction = ledger.tx_begin_write (celerix::store::writer::block_processor);

	celerix::timer<std::chrono::milliseconds> timer;
	timer.start ();

	// Processing blocks
	size_t number_of_blocks_processed = 0;
	size_t number_of_forced_processed = 0;

	processed_batch_t processed;
	for (auto & ctx : batch)
	{
		auto const hash = ctx.block->hash ();
		bool const force = ctx.source == celerix::block_source::forced;

		transaction.refresh_if_needed ();

		if (force)
		{
			number_of_forced_processed++;
			rollback_competitor (transaction, *ctx.block);
		}

		number_of_blocks_processed++;

		auto result = process_one (transaction, ctx, force);
		processed.emplace_back (result, std::move (ctx));
	}

	if (number_of_blocks_processed != 0 && timer.stop () > std::chrono::milliseconds (100))
	{
		logger.debug (celerix::log::type::block_processor, "Processed {} blocks ({} forced) in {} {}", number_of_blocks_processed, number_of_forced_processed, timer.value ().count (), timer.unit ());
	}

	return processed;
}

celerix::block_status celerix::block_processor::process_one (secure::write_transaction const & transaction_a, context const & context, bool const forced_a)
{
	auto block = context.block;
	auto const hash = block->hash ();
	celerix::block_status result = ledger.process (transaction_a, block);

	stats.inc (celerix::stat::type::block_processor_result, to_stat_detail (result));
	stats.inc (celerix::stat::type::block_processor_source, to_stat_detail (context.source));

	logger.trace (celerix::log::type::block_processor, celerix::log::detail::block_processed,
	celerix::log::arg{ "result", result },
	celerix::log::arg{ "source", context.source },
	celerix::log::arg{ "arrival", celerix::log::microseconds (context.arrival) },
	celerix::log::arg{ "forced", forced_a },
	celerix::log::arg{ "block", block });

	switch (result)
	{
		case celerix::block_status::progress:
		{
			unchecked.trigger (hash);

			/*
			 * For send blocks check epoch open unchecked (gap pending).
			 * For state blocks check only send subtype and only if block epoch is not last epoch.
			 * If epoch is last, then pending entry shouldn't trigger same epoch open block for destination account.
			 */
			if (block->type () == celerix::block_type::send || (block->type () == celerix::block_type::state && block->is_send () && std::underlying_type_t<celerix::epoch> (block->sideband ().details.epoch) < std::underlying_type_t<celerix::epoch> (celerix::epoch::max)))
			{
				unchecked.trigger (block->destination ());
			}
			break;
		}
		case celerix::block_status::gap_previous:
		{
			unchecked.put (block->previous (), block);
			stats.inc (celerix::stat::type::ledger, celerix::stat::detail::gap_previous);
			break;
		}
		case celerix::block_status::gap_source:
		{
			release_assert (block->source_field () || block->link_field ());
			unchecked.put (block->source_field ().value_or (block->link_field ().value_or (0).as_block_hash ()), block);
			stats.inc (celerix::stat::type::ledger, celerix::stat::detail::gap_source);
			break;
		}
		case celerix::block_status::gap_epoch_open_pending:
		{
			unchecked.put (block->account_field ().value_or (0), block); // Specific unchecked key starting with epoch open block account public key
			stats.inc (celerix::stat::type::ledger, celerix::stat::detail::gap_source);
			break;
		}
		case celerix::block_status::old:
		{
			stats.inc (celerix::stat::type::ledger, celerix::stat::detail::old);
			break;
		}
		case celerix::block_status::bad_signature:
		{
			break;
		}
		case celerix::block_status::negative_spend:
		{
			break;
		}
		case celerix::block_status::unreceivable:
		{
			break;
		}
		case celerix::block_status::fork:
		{
			stats.inc (celerix::stat::type::ledger, celerix::stat::detail::fork);
			break;
		}
		case celerix::block_status::opened_burn_account:
		{
			break;
		}
		case celerix::block_status::balance_mismatch:
		{
			break;
		}
		case celerix::block_status::representative_mismatch:
		{
			break;
		}
		case celerix::block_status::block_position:
		{
			break;
		}
		case celerix::block_status::insufficient_work:
		{
			break;
		}
	}
	return result;
}

celerix::container_info celerix::block_processor::container_info () const
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };

	celerix::container_info info;
	info.put ("blocks", queue.size ());
	info.put ("forced", queue.size ({ celerix::block_source::forced }));
	info.add ("queue", queue.container_info ());
	info.add ("workers", workers.container_info ());
	return info;
}

/*
 * block_processor::context
 */

celerix::block_processor::context::context (std::shared_ptr<celerix::block> block, celerix::block_source source_a, callback_t callback_a) :
	block{ std::move (block) },
	source{ source_a },
	callback{ std::move (callback_a) }
{
	debug_assert (source != celerix::block_source::unknown);
}

auto celerix::block_processor::context::get_future () -> std::future<result_t>
{
	return promise.get_future ();
}

void celerix::block_processor::context::set_result (result_t const & result)
{
	promise.set_value (result);
}

/*
 * block_processor_config
 */

celerix::block_processor_config::block_processor_config (const celerix::network_constants & network_constants)
{
}

celerix::error celerix::block_processor_config::serialize (celerix::tomlconfig & toml) const
{
	toml.put ("max_peer_queue", max_peer_queue, "Maximum number of blocks to queue from network peers. \ntype:uint64");
	toml.put ("max_system_queue", max_system_queue, "Maximum number of blocks to queue from system components (local RPC, bootstrap). \ntype:uint64");
	toml.put ("priority_live", priority_live, "Priority for live network blocks. Higher priority gets processed more frequently. \ntype:uint64");
	toml.put ("priority_bootstrap", priority_bootstrap, "Priority for bootstrap blocks. Higher priority gets processed more frequently. \ntype:uint64");
	toml.put ("priority_local", priority_local, "Priority for local RPC blocks. Higher priority gets processed more frequently. \ntype:uint64");

	return toml.get_error ();
}

celerix::error celerix::block_processor_config::deserialize (celerix::tomlconfig & toml)
{
	toml.get ("max_peer_queue", max_peer_queue);
	toml.get ("max_system_queue", max_system_queue);
	toml.get ("priority_live", priority_live);
	toml.get ("priority_bootstrap", priority_bootstrap);
	toml.get ("priority_local", priority_local);

	return toml.get_error ();
}

/*
 *
 */

std::string_view celerix::to_string (celerix::block_source source)
{
	return celerix::enum_util::name (source);
}

celerix::stat::detail celerix::to_stat_detail (celerix::block_source type)
{
	return celerix::enum_util::cast<celerix::stat::detail> (type);
}
