#include <celerix/lib/blocks.hpp>
#include <celerix/lib/threading.hpp>
#include <celerix/lib/utility.hpp>
#include <celerix/node/block_processor.hpp>
#include <celerix/node/confirming_set.hpp>
#include <celerix/node/local_block_broadcaster.hpp>
#include <celerix/node/network.hpp>
#include <celerix/node/node.hpp>
#include <celerix/secure/ledger.hpp>

#include <boost/range/iterator_range.hpp>

celerix::local_block_broadcaster::local_block_broadcaster (local_block_broadcaster_config const & config_a, celerix::node & node_a, celerix::block_processor & block_processor_a, celerix::network & network_a, celerix::confirming_set & confirming_set_a, celerix::stats & stats_a, celerix::logger & logger_a, bool enabled_a) :
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
			if (result == celerix::block_status::progress && context.source == celerix::block_source::local)
			{
				release_assert (context.block != nullptr);

				celerix::lock_guard<celerix::mutex> guard{ mutex };

				local_blocks.emplace_back (local_entry{ context.block, std::chrono::steady_clock::now () });
				stats.inc (celerix::stat::type::local_block_broadcaster, celerix::stat::detail::insert);

				// Erase oldest blocks if the queue gets too big
				while (local_blocks.size () > config.max_size)
				{
					stats.inc (celerix::stat::type::local_block_broadcaster, celerix::stat::detail::erase_oldest);
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

	block_processor.rolled_back.add ([this] (auto const & blocks, auto const & rollback_root) {
		celerix::lock_guard<celerix::mutex> guard{ mutex };
		for (auto const & block : blocks)
		{
			auto erased = local_blocks.get<tag_hash> ().erase (block->hash ());
			stats.add (celerix::stat::type::local_block_broadcaster, celerix::stat::detail::rollback, erased);
		}
	});

	confirming_set.cemented_observers.add ([this] (auto const & block) {
		celerix::lock_guard<celerix::mutex> guard{ mutex };
		auto erased = local_blocks.get<tag_hash> ().erase (block->hash ());
		stats.add (celerix::stat::type::local_block_broadcaster, celerix::stat::detail::cemented, erased);
	});
}

celerix::local_block_broadcaster::~local_block_broadcaster ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void celerix::local_block_broadcaster::start ()
{
	if (!enabled)
	{
		return;
	}

	debug_assert (!thread.joinable ());

	thread = std::thread{ [this] () {
		celerix::thread_role::set (celerix::thread_role::name::local_block_broadcasting);
		run ();
	} };
}

void celerix::local_block_broadcaster::stop ()
{
	{
		celerix::lock_guard<celerix::mutex> lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	celerix::join_or_pass (thread);
}

bool celerix::local_block_broadcaster::contains (celerix::block_hash const & hash) const
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	return local_blocks.get<tag_hash> ().contains (hash);
}

size_t celerix::local_block_broadcaster::size () const
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	return local_blocks.size ();
}

void celerix::local_block_broadcaster::run ()
{
	celerix::unique_lock<celerix::mutex> lock{ mutex };
	while (!stopped)
	{
		condition.wait_for (lock, 1s);
		debug_assert ((std::this_thread::yield (), true)); // Introduce some random delay in debug builds

		if (!stopped && !local_blocks.empty ())
		{
			stats.inc (celerix::stat::type::local_block_broadcaster, celerix::stat::detail::loop);

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

std::chrono::milliseconds celerix::local_block_broadcaster::rebroadcast_interval (unsigned rebroadcasts) const
{
	return std::min (config.rebroadcast_interval * rebroadcasts, config.max_rebroadcast_interval);
}

void celerix::local_block_broadcaster::run_broadcasts (celerix::unique_lock<celerix::mutex> & lock)
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

		logger.debug (celerix::log::type::local_block_broadcaster, "Broadcasting block: {} (rebroadcasts so far: {})",
		entry.block->hash ().to_string (),
		entry.rebroadcasts);

		stats.inc (celerix::stat::type::local_block_broadcaster, celerix::stat::detail::broadcast, celerix::stat::dir::out);
		network.flood_block_initial (entry.block);
	}
}

void celerix::local_block_broadcaster::cleanup (celerix::unique_lock<celerix::mutex> & lock)
{
	debug_assert (!mutex.try_lock ());

	// Copy the local blocks to avoid holding the mutex during IO
	auto local_blocks_copy = local_blocks;

	lock.unlock ();

	std::set<celerix::block_hash> already_confirmed;
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
				stats.inc (celerix::stat::type::local_block_broadcaster, celerix::stat::detail::already_confirmed);
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

celerix::container_info celerix::local_block_broadcaster::container_info () const
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };

	celerix::container_info info;
	info.put ("local", local_blocks);
	return info;
}
