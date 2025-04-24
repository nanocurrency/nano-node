#include <nano/lib/blocks.hpp>
#include <nano/lib/threading.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/block_processor.hpp>
#include <nano/node/cementing_set.hpp>
#include <nano/node/ledger_notifications.hpp>
#include <nano/node/local_block_broadcaster.hpp>
#include <nano/node/network.hpp>
#include <nano/node/node.hpp>
#include <nano/secure/ledger.hpp>

#include <boost/range/iterator_range.hpp>

nano::local_block_broadcaster::local_block_broadcaster (local_block_broadcaster_config const & config_a, nano::node & node_a, nano::ledger_notifications & ledger_notifications_a, nano::network & network_a, nano::cementing_set & cementing_set_a, nano::stats & stats_a, nano::logger & logger_a, bool enabled_a) :
	config{ config_a },
	node{ node_a },
	ledger_notifications{ ledger_notifications_a },
	network{ network_a },
	cementing_set{ cementing_set_a },
	stats{ stats_a },
	logger{ logger_a },
	enabled{ enabled_a },
	limiter{ config.broadcast_rate_limit, config.broadcast_rate_burst_ratio }
{
	if (!enabled)
	{
		return;
	}

	ledger_notifications.blocks_processed.add ([this] (auto const & batch) {
		bool should_notify = false;
		for (auto const & [result, context] : batch)
		{
			// Only rebroadcast local blocks that were successfully processed (no forks or gaps)
			if (result == nano::block_status::progress && context.source == nano::block_source::local)
			{
				release_assert (context.block != nullptr);

				nano::lock_guard<nano::mutex> guard{ mutex };

				local_blocks.emplace_back (local_entry{ context.block, std::chrono::steady_clock::now () });
				stats.inc (nano::stat::type::local_block_broadcaster, nano::stat::detail::insert);

				// Erase oldest blocks if the queue gets too big
				while (local_blocks.size () > config.max_size)
				{
					stats.inc (nano::stat::type::local_block_broadcaster, nano::stat::detail::erase_oldest);
					local_blocks.pop_front ();
				}

				should_notify = true;
			}
		}
		if (should_notify)
		{
			condition.notify_all ();
		}
	});

	ledger_notifications.blocks_rolled_back.add ([this] (auto const & blocks, auto const & rollback_root) {
		nano::lock_guard<nano::mutex> guard{ mutex };
		for (auto const & block : blocks)
		{
			auto erased = local_blocks.get<tag_hash> ().erase (block->hash ());
			stats.add (nano::stat::type::local_block_broadcaster, nano::stat::detail::rollback, erased);
		}
	});

	cementing_set.cemented_observers.add ([this] (auto const & block) {
		nano::lock_guard<nano::mutex> guard{ mutex };
		auto erased = local_blocks.get<tag_hash> ().erase (block->hash ());
		stats.add (nano::stat::type::local_block_broadcaster, nano::stat::detail::cemented, erased);
	});
}

nano::local_block_broadcaster::~local_block_broadcaster ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void nano::local_block_broadcaster::start ()
{
	if (!enabled)
	{
		return;
	}

	debug_assert (!thread.joinable ());

	thread = std::thread{ [this] () {
		nano::thread_role::set (nano::thread_role::name::local_block_broadcasting);
		run ();
	} };
}

void nano::local_block_broadcaster::stop ()
{
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	nano::join_or_pass (thread);
}

bool nano::local_block_broadcaster::contains (nano::block_hash const & hash) const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return local_blocks.get<tag_hash> ().contains (hash);
}

size_t nano::local_block_broadcaster::size () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return local_blocks.size ();
}

void nano::local_block_broadcaster::run ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		condition.wait_for (lock, 1s);
		debug_assert ((std::this_thread::yield (), true)); // Introduce some random delay in debug builds

		if (!stopped && !local_blocks.empty ())
		{
			stats.inc (nano::stat::type::local_block_broadcaster, nano::stat::detail::loop);

			if (cleanup_interval.elapse (config.cleanup_interval))
			{
				cleanup (lock);
				debug_assert (lock.owns_lock ());
			}

			if (log_interval.elapse (15s))
			{
				logger.info (nano::log::type::local_block_broadcaster, "{} blocks in local broadcasting set", local_blocks.size ());
			}

			run_broadcasts (lock);
			debug_assert (!lock.owns_lock ());
			lock.lock ();
		}
	}
}

std::chrono::milliseconds nano::local_block_broadcaster::rebroadcast_interval (unsigned rebroadcasts) const
{
	return std::min (config.rebroadcast_interval * rebroadcasts, config.max_rebroadcast_interval);
}

void nano::local_block_broadcaster::run_broadcasts (nano::unique_lock<nano::mutex> & lock)
{
	debug_assert (lock.owns_lock ());
	debug_assert (!mutex.try_lock ());

	std::deque<local_entry> to_broadcast;

	auto const now = std::chrono::steady_clock::now ();

	// Iterate blocks with next_broadcast <= now
	auto & by_broadcast = local_blocks.get<tag_broadcast> ();
	for (auto const & entry : boost::make_iterator_range (by_broadcast.begin (), by_broadcast.upper_bound (now)))
	{
		debug_assert (entry.next_broadcast <= now);
		release_assert (entry.block != nullptr);
		to_broadcast.push_back (entry);
	}

	// Modify multi index container outside of the loop to avoid invalidating iterators
	auto & by_hash = local_blocks.get<tag_hash> ();
	for (auto const & entry : to_broadcast)
	{
		auto it = by_hash.find (entry.hash ());
		release_assert (it != by_hash.end ());
		bool success = by_hash.modify (it, [this, now] (auto & entry) {
			entry.rebroadcasts += 1;
			entry.last_broadcast = now;
			entry.next_broadcast = now + rebroadcast_interval (entry.rebroadcasts);
		});
		release_assert (success, "modify failed"); // Should never fail
	}

	lock.unlock ();

	for (auto const & entry : to_broadcast)
	{
		while (!limiter.should_pass (1))
		{
			std::this_thread::sleep_for (std::chrono::milliseconds{ 100 });
			if (stopped)
			{
				return;
			}
		}

		logger.debug (nano::log::type::local_block_broadcaster, "Broadcasting block: {} (rebroadcasts so far: {})",
		entry.block->hash (),
		entry.rebroadcasts);

		stats.inc (nano::stat::type::local_block_broadcaster, nano::stat::detail::broadcast);

		auto sent = network.flood_block_initial (entry.block);

		stats.add (nano::stat::type::local_block_broadcaster, nano::stat::detail::sent, sent);
	}
}

void nano::local_block_broadcaster::cleanup (nano::unique_lock<nano::mutex> & lock)
{
	debug_assert (!mutex.try_lock ());

	// Copy the local blocks to avoid holding the mutex during IO
	auto local_blocks_copy = local_blocks;

	lock.unlock ();

	std::set<nano::block_hash> already_confirmed;
	{
		auto transaction = node.ledger.tx_begin_read ();
		for (auto const & entry : local_blocks_copy)
		{
			// This block has never been broadcasted, keep it so it's broadcasted at least once
			if (entry.last_broadcast == std::chrono::steady_clock::time_point{})
			{
				continue;
			}
			if (node.block_confirmed_or_being_confirmed (transaction, entry.block->hash ()))
			{
				stats.inc (nano::stat::type::local_block_broadcaster, nano::stat::detail::already_confirmed);
				already_confirmed.insert (entry.block->hash ());
			}
		}
	}

	lock.lock ();

	// Erase blocks that have been confirmed
	erase_if (local_blocks, [&already_confirmed] (auto const & entry) {
		return already_confirmed.contains (entry.block->hash ());
	});
}

nano::container_info nano::local_block_broadcaster::container_info () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };

	nano::container_info info;
	info.put ("local", local_blocks);
	return info;
}

/*
 * local_block_broadcaster_config
 */

nano::error nano::local_block_broadcaster_config::serialize (nano::tomlconfig & toml) const
{
	toml.put ("max_size", max_size, "Maximum number of blocks to keep in the local block broadcaster set. \ntype:uint64");
	toml.put ("rebroadcast_interval", rebroadcast_interval.count (), "Interval between rebroadcasts of the same block. Interval increases with each rebroadcast. \ntype:seconds");
	toml.put ("max_rebroadcast_interval", max_rebroadcast_interval.count (), "Maximum interval between rebroadcasts of the same block. \ntype:seconds");
	toml.put ("broadcast_rate_limit", broadcast_rate_limit, "Rate limit for broadcasting blocks. \ntype:uint64");
	toml.put ("broadcast_rate_burst_ratio", broadcast_rate_burst_ratio, "Burst ratio for broadcasting blocks. \ntype:double");
	toml.put ("cleanup_interval", cleanup_interval.count (), "Cleanup interval of the local blocks broadcaster set. \ntype:seconds");

	return toml.get_error ();
}

nano::error nano::local_block_broadcaster_config::deserialize (nano::tomlconfig & toml)
{
	toml.get ("max_size", max_size);
	toml.get_duration ("rebroadcast_interval", rebroadcast_interval);
	toml.get_duration ("max_rebroadcast_interval", max_rebroadcast_interval);
	toml.get ("broadcast_rate_limit", broadcast_rate_limit);
	toml.get ("broadcast_rate_burst_ratio", broadcast_rate_burst_ratio);
	toml.get_duration ("cleanup_interval", cleanup_interval);

	return toml.get_error ();
}