#include <nano/lib/blocks.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/lib/threading.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/election.hpp>
#include <nano/node/node.hpp>
#include <nano/node/scheduler/manual.hpp>

nano::scheduler::manual::manual (nano::node & node) :
	node{ node }
{
}

nano::scheduler::manual::~manual ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void nano::scheduler::manual::start ()
{
	debug_assert (!thread.joinable ());

	thread = std::thread{ [this] () {
		nano::thread_role::set (nano::thread_role::name::scheduler_manual);
		run ();
	} };
}

void nano::scheduler::manual::stop ()
{
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	nano::join_or_pass (thread);
}

void nano::scheduler::manual::notify ()
{
	condition.notify_all ();
}

auto nano::scheduler::manual::push (std::shared_ptr<nano::block> const & block) -> std::future<std::shared_ptr<nano::election>>
{
	nano::lock_guard<nano::mutex> lock{ mutex };

	// Check if block already exists
	auto & hash_index = queue.get<tag_hash> ();

	if (hash_index.contains (block->hash ()))
	{
		// Block already exists, return future that immediately resolves to nullptr
		std::promise<std::shared_ptr<nano::election>> promise;
		auto future = promise.get_future ();
		promise.set_value (nullptr);
		return future;
	}

	// Create entry and get future before inserting
	entry new_entry{ block };
	auto future = new_entry.promise.get_future ();

	auto [it, inserted] = queue.push_back (std::move (new_entry));
	debug_assert (inserted);

	condition.notify_all ();
	return future;
}

bool nano::scheduler::manual::contains (nano::block_hash const & hash) const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	auto & hash_index = queue.get<tag_hash> ();
	return hash_index.contains (hash);
}

bool nano::scheduler::manual::predicate () const
{
	debug_assert (!mutex.try_lock ());
	return !queue.empty ();
}

void nano::scheduler::manual::run ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		condition.wait (lock, [this] () {
			return stopped || predicate ();
		});

		if (stopped)
		{
			return;
		}

		debug_assert ((std::this_thread::yield (), true));

		if (predicate ())
		{
			node.stats.inc (nano::stat::type::election_scheduler, nano::stat::detail::loop);

			auto promise = std::move (queue.front ().promise);
			auto block = queue.front ().block;
			queue.pop_front ();

			lock.unlock ();

			auto result = node.active.insert (block, nano::election_behavior::manual);
			if (result.inserted)
			{
				node.stats.inc (nano::stat::type::election_scheduler, nano::stat::detail::insert_manual);
			}
			if (result.election != nullptr)
			{
				result.election->transition_active ();
			}

			// Fulfill the promise
			promise.set_value (result.election);

			lock.lock ();
		}
	}
}

nano::container_info nano::scheduler::manual::container_info () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };

	nano::container_info info;
	info.put ("queue", queue);
	return info;
}