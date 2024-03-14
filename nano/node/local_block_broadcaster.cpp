#include <nano/lib/blocks.hpp>
#include <nano/lib/threading.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/blockprocessor.hpp>
#include <nano/node/local_block_broadcaster.hpp>
#include <nano/node/network.hpp>
#include <nano/node/node.hpp>

nano::local_block_broadcaster::local_block_broadcaster (nano::node & node_a, nano::block_processor & block_processor_a, nano::network & network_a, nano::stats & stats_a, bool enabled_a) :
	node{ node_a },
	block_processor{ block_processor_a },
	network{ network_a },
	stats{ stats_a },
	enabled{ enabled_a }
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
				nano::lock_guard<nano::mutex> guard{ mutex };
				local_blocks.emplace_back (local_entry{ context.block, std::chrono::steady_clock::now () });
				stats.inc (nano::stat::type::local_block_broadcaster, nano::stat::detail::insert);
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
		stats.add (nano::stat::type::local_block_broadcaster, nano::stat::detail::rollback, nano::stat::dir::in, erased);
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

void nano::local_block_broadcaster::run ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		stats.inc (nano::stat::type::local_block_broadcaster, nano::stat::detail::loop);

		condition.wait_for (lock, check_interval);
		debug_assert ((std::this_thread::yield (), true)); // Introduce some random delay in debug builds

		if (!stopped)
		{
			cleanup ();
			run_broadcasts (lock);
			debug_assert (lock.owns_lock ());
		}
	}
}

void nano::local_block_broadcaster::run_broadcasts (nano::unique_lock<nano::mutex> & lock)
{
	debug_assert (lock.owns_lock ());

	std::vector<std::shared_ptr<nano::block>> to_broadcast;

	auto const now = std::chrono::steady_clock::now ();
	for (auto & entry : local_blocks)
	{
		if (elapsed (entry.last_broadcast, broadcast_interval, now))
		{
			entry.last_broadcast = now;
			to_broadcast.push_back (entry.block);
		}
	}

	lock.unlock ();

	for (auto const & block : to_broadcast)
	{
		while (!limiter.should_pass (1))
		{
			std::this_thread::sleep_for (std::chrono::milliseconds{ 100 });
			if (stopped)
			{
				return;
			}
		}

		stats.inc (nano::stat::type::local_block_broadcaster, nano::stat::detail::broadcast, nano::stat::dir::out);

		network.flood_block_initial (block);
	}

	lock.lock ();
}

void nano::local_block_broadcaster::cleanup ()
{
	debug_assert (!mutex.try_lock ());

	// Erase oldest blocks if the queue gets too big
	while (local_blocks.size () > max_size)
	{
		stats.inc (nano::stat::type::local_block_broadcaster, nano::stat::detail::erase_oldest);
		local_blocks.pop_front ();
	}

	// TODO: Mutex is held during IO, but it should be fine since it's not performance critical
	auto transaction = node.store.tx_begin_read ();
	erase_if (local_blocks, [this, &transaction] (auto const & entry) {
		transaction.refresh_if_needed ();

		if (entry.last_broadcast == std::chrono::steady_clock::time_point{})
		{
			// This block has never been broadcasted, keep it so it's broadcasted at least once
			return false;
		}
		if (node.block_confirmed_or_being_confirmed (transaction, entry.block->hash ()))
		{
			stats.inc (nano::stat::type::local_block_broadcaster, nano::stat::detail::erase_confirmed);
			return true;
		}
		return false;
	});
}

std::unique_ptr<nano::container_info_component> nano::local_block_broadcaster::collect_container_info (const std::string & name) const
{
	nano::lock_guard<nano::mutex> guard{ mutex };

	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "local", local_blocks.size (), sizeof (decltype (local_blocks)::value_type) }));
	return composite;
}

nano::block_hash nano::local_block_broadcaster::local_entry::hash () const
{
	return block->hash ();
}
