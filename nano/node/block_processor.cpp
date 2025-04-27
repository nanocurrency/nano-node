#include <nano/lib/block_type.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/config.hpp>
#include <nano/lib/enum_util.hpp>
#include <nano/lib/threading.hpp>
#include <nano/lib/timer.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/block_processor.hpp>
#include <nano/node/ledger_notifications.hpp>
#include <nano/node/local_vote_history.hpp>
#include <nano/node/node.hpp>
#include <nano/node/unchecked_map.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/store/component.hpp>

#include <utility>

/*
 * block_processor
 */

nano::block_processor::block_processor (nano::node_config const & node_config_a, nano::ledger & ledger_a, nano::ledger_notifications & ledger_notifications_a, nano::unchecked_map & unchecked_a, nano::stats & stats_a, nano::logger & logger_a) :
	config{ node_config_a.block_processor },
	node_config{ node_config_a },
	network_params{ node_config.network_params },
	ledger{ ledger_a },
	ledger_notifications{ ledger_notifications_a },
	unchecked{ unchecked_a },
	stats{ stats_a },
	logger{ logger_a }
{
	queue.max_size_query = [this] (auto const & origin) {
		switch (origin.source)
		{
			case nano::block_source::live:
			case nano::block_source::live_originator:
				return config.max_peer_queue;
			default:
				return config.max_system_queue;
		}
	};

	queue.priority_query = [this] (auto const & origin) -> size_t {
		switch (origin.source)
		{
			case nano::block_source::live:
			case nano::block_source::live_originator:
				return config.priority_live;
			case nano::block_source::bootstrap:
			case nano::block_source::bootstrap_legacy:
			case nano::block_source::unchecked:
				return config.priority_bootstrap;
			case nano::block_source::local:
				return config.priority_local;
			default:
				return config.priority_system;
		}
	};

	// Requeue blocks that could not be immediately processed
	unchecked.satisfied.add ([this] (nano::unchecked_info const & info) {
		add (info.block, nano::block_source::unchecked);
	});
}

nano::block_processor::~block_processor ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void nano::block_processor::start ()
{
	debug_assert (!thread.joinable ());

	boost::thread::attributes attrs;
	attrs.set_stack_size (nano::ledger_thread_stack_size ());

	thread = boost::thread (attrs, [this] () {
		nano::thread_role::set (nano::thread_role::name::block_processing);
		run ();
	});
}

void nano::block_processor::stop ()
{
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	if (thread.joinable ())
	{
		thread.join ();
	}
}

// TODO: Remove and replace all checks with calls to size (block_source)
std::size_t nano::block_processor::size () const
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	return queue.size ();
}

std::size_t nano::block_processor::size (nano::block_source source) const
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	return queue.size ({ source });
}

bool nano::block_processor::add (std::shared_ptr<nano::block> const & block, block_source const source, std::shared_ptr<nano::transport::channel> const & channel, std::function<void (nano::block_status)> callback)
{
	if (network_params.work.validate_entry (*block)) // true => error
	{
		stats.inc (nano::stat::type::block_processor, nano::stat::detail::insufficient_work);
		return false; // Not added
	}

	stats.inc (nano::stat::type::block_processor, nano::stat::detail::process);
	logger.debug (nano::log::type::block_processor, "Processing block (async): {} (source: {} {})",
	block->hash ().to_string (),
	to_string (source),
	channel ? channel->to_string () : "<unknown>"); // TODO: Lazy eval

	return add_impl ({ block, source, std::move (callback) }, channel);
}

std::optional<nano::block_status> nano::block_processor::add_blocking (std::shared_ptr<nano::block> const & block, block_source const source)
{
	stats.inc (nano::stat::type::block_processor, nano::stat::detail::process_blocking);
	logger.debug (nano::log::type::block_processor, "Processing block (blocking): {} (source: {})", block->hash ().to_string (), to_string (source));

	nano::block_context ctx{ block, source };
	auto future = ctx.get_future ();
	add_impl (std::move (ctx));

	try
	{
		future.wait ();
		return future.get ();
	}
	catch (std::future_error const &)
	{
		stats.inc (nano::stat::type::block_processor, nano::stat::detail::process_blocking_timeout);
		logger.error (nano::log::type::block_processor, "Block dropped when processing: {}", block->hash ().to_string ());
	}

	return std::nullopt;
}

void nano::block_processor::force (std::shared_ptr<nano::block> const & block_a)
{
	stats.inc (nano::stat::type::block_processor, nano::stat::detail::force);
	logger.debug (nano::log::type::block_processor, "Forcing block: {}", block_a->hash ().to_string ());

	add_impl ({ block_a, block_source::forced });
}

bool nano::block_processor::add_impl (nano::block_context ctx, std::shared_ptr<nano::transport::channel> const & channel)
{
	auto const source = ctx.source;
	bool added = false;
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		added = queue.push (std::move (ctx), { source, channel });
	}
	if (added)
	{
		condition.notify_all ();
	}
	else
	{
		stats.inc (nano::stat::type::block_processor, nano::stat::detail::overfill);
		stats.inc (nano::stat::type::block_processor_overfill, to_stat_detail (source));
	}
	return added;
}

void nano::block_processor::rollback_competitor (secure::write_transaction & transaction, nano::block const & fork_block)
{
	auto const hash = fork_block.hash ();
	auto const successor_hash = ledger.any.block_successor (transaction, fork_block.qualified_root ());
	auto const successor = successor_hash ? ledger.any.block_get (transaction, successor_hash.value ()) : nullptr;
	if (successor != nullptr && successor->hash () != hash)
	{
		// Replace our block with the winner and roll back any dependent blocks
		logger.debug (nano::log::type::block_processor, "Rolling back: {} and replacing with: {}", successor->hash ().to_string (), hash.to_string ());

		std::deque<std::shared_ptr<nano::block>> rollback_list;
		bool error = ledger.rollback (transaction, successor->hash (), rollback_list);
		if (error)
		{
			stats.inc (nano::stat::type::ledger, nano::stat::detail::rollback_failed);
			logger.warn (nano::log::type::block_processor, "Failed to roll back: {} (succeeded with {} dependents)", successor->hash ().to_string (), rollback_list.size ());
		}
		else
		{
			stats.inc (nano::stat::type::ledger, nano::stat::detail::rollback);
			logger.debug (nano::log::type::block_processor, "Rolled back {} with {} dependents", successor->hash ().to_string (), rollback_list.size ());
		}

		if (!rollback_list.empty ())
		{
			// Notify observers of the rolled back blocks on a background thread while not holding the ledger write lock
			ledger_notifications.notify_rolled_back (transaction, std::move (rollback_list), fork_block.qualified_root (), [this] {
				stats.inc (nano::stat::type::block_processor, nano::stat::detail::notify_rolled_back);
			});
		}
	}
}

double nano::block_processor::backlog_factor () const
{
	auto const backlog = ledger.backlog_count ();
	if (node_config.max_backlog == 0 || backlog <= node_config.max_backlog * config.backlog_threshold)
	{
		return 0.0;
	}
	return std::max (1.0, static_cast<double> (backlog) / static_cast<double> (node_config.max_backlog * config.backlog_threshold));
}

void nano::block_processor::wait_backlog (nano::unique_lock<nano::mutex> & lock)
{
	debug_assert (lock.owns_lock ());
	debug_assert (!mutex.try_lock ());

	double const factor = backlog_factor ();

	if (factor < 1.0)
	{
		return;
	}

	auto scaling = [] (double factor) {
		// This uses a power of approximately 3.32, which gives ~1x at 1.0 and ~10x at 2.0
		return std::pow (factor, 3.32);
	};
	auto const throttle_wait = std::min (config.backlog_throttle * scaling (factor), config.backlog_throttle_max * 1.0);

	if (log_backlog_interval.elapse (15s))
	{
		logger.warn (nano::log::type::block_processor, "Backlog exceeded, throttling for {}ms (backlog factor: {})",
		throttle_wait.count (),
		factor);
	}

	stats.inc (nano::stat::type::block_processor, nano::stat::detail::cooldown_backlog);

	condition.wait_for (lock, throttle_wait, [&] {
		return stopped || backlog_factor () < 1.0;
	});
}

void nano::block_processor::run ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		condition.wait (lock, [this] {
			return stopped || !queue.empty ();
		});

		if (stopped)
		{
			return;
		}

		if (config.enable_throttling)
		{
			wait_backlog (lock);
			debug_assert (lock.owns_lock ());
		}

		lock.unlock ();

		// It's possible that ledger processing happens faster than the notifications can be processed by other components, cooldown here
		ledger_notifications.wait ([this] {
			stats.inc (nano::stat::type::block_processor, nano::stat::detail::cooldown);
		});

		lock.lock ();

		if (!queue.empty ())
		{
			// Only log if component is under pressure
			if (queue.size () > nano::queue_warning_threshold () && log_processing_interval.elapse (15s))
			{
				logger.info (nano::log::type::block_processor, "{} blocks ({} forced) in processing queue",
				queue.size (),
				queue.size ({ nano::block_source::forced }));
			}

			process_batch (lock);
			debug_assert (!lock.owns_lock ());
			lock.lock ();
		}
	}
}

auto nano::block_processor::next () -> nano::block_context
{
	debug_assert (!mutex.try_lock ());
	debug_assert (!queue.empty ()); // This should be checked before calling next

	if (!queue.empty ())
	{
		auto [request, origin] = queue.next ();
		release_assert (origin.source != nano::block_source::forced || request.source == nano::block_source::forced);
		return std::move (request);
	}

	release_assert (false, "next() called when no blocks are ready");
}

auto nano::block_processor::next_batch (size_t max_count) -> std::deque<nano::block_context>
{
	debug_assert (!mutex.try_lock ());
	debug_assert (!queue.empty ());

	queue.periodic_update ();

	std::deque<nano::block_context> results;
	while (!queue.empty () && results.size () < max_count)
	{
		results.push_back (next ());
	}
	return results;
}

void nano::block_processor::process_batch (nano::unique_lock<nano::mutex> & lock)
{
	debug_assert (lock.owns_lock ());
	debug_assert (!mutex.try_lock ());
	debug_assert (!queue.empty ());

	auto batch = next_batch (config.batch_size);

	lock.unlock ();

	auto transaction = ledger.tx_begin_write (nano::store::writer::block_processor);

	nano::timer<std::chrono::milliseconds> timer;
	timer.start ();

	// Processing blocks
	size_t number_of_blocks_processed = 0;
	size_t number_of_forced_processed = 0;

	std::deque<std::pair<nano::block_status, nano::block_context>> processed;

	for (auto & ctx : batch)
	{
		auto const hash = ctx.block->hash ();
		bool const force = ctx.source == nano::block_source::forced;

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
		logger.debug (nano::log::type::block_processor, "Processed {} blocks ({} forced) in {} {}", number_of_blocks_processed, number_of_forced_processed, timer.value ().count (), timer.unit ());
	}

	// Queue notifications to be dispatched in the background
	ledger_notifications.notify_processed (transaction, std::move (processed), [this] {
		stats.inc (nano::stat::type::block_processor, nano::stat::detail::notify_processed);
	});
}

nano::block_status nano::block_processor::process_one (secure::write_transaction const & transaction_a, nano::block_context const & context, bool const forced_a)
{
	auto block = context.block;
	auto const hash = block->hash ();
	nano::block_status result = ledger.process (transaction_a, block);

	stats.inc (nano::stat::type::block_processor_result, to_stat_detail (result));
	stats.inc (nano::stat::type::block_processor_source, to_stat_detail (context.source));

	logger.trace (nano::log::type::block_processor, nano::log::detail::block_processed,
	nano::log::arg{ "result", result },
	nano::log::arg{ "source", context.source },
	nano::log::arg{ "arrival", nano::log::microseconds (context.arrival) },
	nano::log::arg{ "forced", forced_a },
	nano::log::arg{ "block", block });

	switch (result)
	{
		case nano::block_status::progress:
		{
			unchecked.trigger (hash);

			/*
			 * For send blocks check epoch open unchecked (gap pending).
			 * For state blocks check only send subtype and only if block epoch is not last epoch.
			 * If epoch is last, then pending entry shouldn't trigger same epoch open block for destination account.
			 */
			if (block->type () == nano::block_type::send || (block->type () == nano::block_type::state && block->is_send () && std::underlying_type_t<nano::epoch> (block->sideband ().details.epoch) < std::underlying_type_t<nano::epoch> (nano::epoch::max)))
			{
				unchecked.trigger (block->destination ());
			}
			break;
		}
		case nano::block_status::gap_previous:
		{
			unchecked.put (block->previous (), block);
			stats.inc (nano::stat::type::ledger, nano::stat::detail::gap_previous);
			break;
		}
		case nano::block_status::gap_source:
		{
			release_assert (block->source_field () || block->link_field ());
			unchecked.put (block->source_field ().value_or (block->link_field ().value_or (0).as_block_hash ()), block);
			stats.inc (nano::stat::type::ledger, nano::stat::detail::gap_source);
			break;
		}
		case nano::block_status::gap_epoch_open_pending:
		{
			unchecked.put (block->account_field ().value_or (0), block); // Specific unchecked key starting with epoch open block account public key
			stats.inc (nano::stat::type::ledger, nano::stat::detail::gap_source);
			break;
		}
		case nano::block_status::old:
		{
			stats.inc (nano::stat::type::ledger, nano::stat::detail::old);
			break;
		}
		// These are unexpected and indicate erroneous/malicious behavior, log debug info to highlight the issue
		case nano::block_status::bad_signature:
		{
			logger.debug (nano::log::type::block_processor, "Block signature is invalid: {}", hash);
			break;
		}
		case nano::block_status::negative_spend:
		{
			logger.debug (nano::log::type::block_processor, "Block spends negative amount: {}", hash);
			break;
		}
		case nano::block_status::unreceivable:
		{
			logger.debug (nano::log::type::block_processor, "Block is unreceivable: {}", hash);
			break;
		}
		case nano::block_status::fork:
		{
			stats.inc (nano::stat::type::ledger, nano::stat::detail::fork);
			logger.debug (nano::log::type::block_processor, "Block is a fork: {}", hash);
			break;
		}
		case nano::block_status::opened_burn_account:
		{
			logger.debug (nano::log::type::block_processor, "Block opens burn account: {}", hash);
			break;
		}
		case nano::block_status::balance_mismatch:
		{
			logger.debug (nano::log::type::block_processor, "Block balance mismatch: {}", hash);
			break;
		}
		case nano::block_status::representative_mismatch:
		{
			logger.debug (nano::log::type::block_processor, "Block representative mismatch: {}", hash);
			break;
		}
		case nano::block_status::block_position:
		{
			logger.debug (nano::log::type::block_processor, "Block is in incorrect position: {}", hash);
			break;
		}
		case nano::block_status::insufficient_work:
		{
			logger.debug (nano::log::type::block_processor, "Block has insufficient work: {}", hash);
			break;
		}
	}
	return result;
}

nano::container_info nano::block_processor::container_info () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };

	nano::container_info info;
	info.put ("blocks", queue.size ());
	info.put ("forced", queue.size ({ nano::block_source::forced }));
	info.add ("queue", queue.container_info ());
	return info;
}

/*
 * block_processor_config
 */

nano::block_processor_config::block_processor_config (const nano::network_constants & network_constants)
{
}

nano::error nano::block_processor_config::serialize (nano::tomlconfig & toml) const
{
	toml.put ("max_peer_queue", max_peer_queue, "Maximum number of blocks to queue from network peers. \ntype:uint64");
	toml.put ("max_system_queue", max_system_queue, "Maximum number of blocks to queue from system components (local RPC, bootstrap). \ntype:uint64");
	toml.put ("priority_live", priority_live, "Priority for live network blocks. Higher priority gets processed more frequently. \ntype:uint64");
	toml.put ("priority_bootstrap", priority_bootstrap, "Priority for bootstrap blocks. Higher priority gets processed more frequently. \ntype:uint64");
	toml.put ("priority_local", priority_local, "Priority for local RPC blocks. Higher priority gets processed more frequently. \ntype:uint64");
	toml.put ("enable_throttling", enable_throttling, "Enable throttling of block processing when backlog exceeds threshold. \ntype:bool");
	toml.put ("backlog_threshold", backlog_threshold, "Threshold for backlog before throttling is applied. \ntype:double");
	toml.put ("backlog_throttle", backlog_throttle.count (), "Throttling interval for processing blocks when backlog is above threshold. \ntype:milliseconds");
	toml.put ("backlog_throttle_max", backlog_throttle_max.count (), "Maximum throttling interval for processing blocks when backlog is above threshold. \ntype:milliseconds");

	return toml.get_error ();
}

nano::error nano::block_processor_config::deserialize (nano::tomlconfig & toml)
{
	toml.get ("max_peer_queue", max_peer_queue);
	toml.get ("max_system_queue", max_system_queue);
	toml.get ("priority_live", priority_live);
	toml.get ("priority_bootstrap", priority_bootstrap);
	toml.get ("priority_local", priority_local);
	toml.get ("enable_throttling", enable_throttling);
	toml.get ("backlog_threshold", backlog_threshold);
	toml.get_duration ("backlog_throttle", backlog_throttle);
	toml.get_duration ("backlog_throttle_max", backlog_throttle_max);

	return toml.get_error ();
}
