#include <nano/lib/blocks.hpp>
#include <nano/lib/threading.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/blockprocessor.hpp>
#include <nano/node/confirming_set.hpp>
#include <nano/node/local_block_broadcaster.hpp>
#include <nano/node/network.hpp>
#include <nano/node/node.hpp>
#include <nano/secure/ledger.hpp>

nano::local_block_broadcaster::local_block_broadcaster (local_block_broadcaster_config const & config_a, nano::node & node_a, nano::block_processor & block_processor_a, nano::network & network_a, nano::confirming_set & confirming_set_a, nano::stats & stats_a, nano::logger & logger_a, bool enabled_a) :
	config{ config_a },
	node{ node_a },
	block_processor{ block_processor_a },
	network{ network_a },
	confirming_set{ confirming_set_a },
	stats{ stats_a },
	logger{ logger_a },
	enabled{ enabled_a },
	limiter{ config.broadcast_rate_limit, config.broadcast_rate_burst_ratio }
{
	if (!enabled)
	{
		return;
	}

	block_processor.batch_processed.add ([this] (auto const & batch) {
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

	block_processor.rolled_back.add ([this] (auto const & block) {
		nano::lock_guard<nano::mutex> guard{ mutex };
		auto erased = local_blocks.get<tag_hash> ().erase (block->hash ());
		stats.add (nano::stat::type::local_block_broadcaster, nano::stat::detail::rollback, erased);
	});

	confirming_set.cemented_observers.add ([this] (auto const & block) {
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

			if (cleanup_interval.elapsed (config.cleanup_interval))
			{
				cleanup (lock);
				debug_assert (lock.owns_lock ());
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
		entry.block->hash ().to_string (),
		entry.rebroadcasts);

		stats.inc (nano::stat::type::local_block_broadcaster, nano::stat::detail::broadcast, nano::stat::dir::out);
		network.flood_block_initial (entry.block);
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