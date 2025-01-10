#include <celerix/node/active_elections.hpp>
#include <celerix/node/election.hpp>
#include <celerix/node/node.hpp>
#include <celerix/node/scheduler/manual.hpp>

celerix::scheduler::manual::manual (celerix::node & node) :
	node{ node }
{
}

celerix::scheduler::manual::~manual ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void celerix::scheduler::manual::start ()
{
	debug_assert (!thread.joinable ());

	thread = std::thread{ [this] () {
		celerix::thread_role::set (celerix::thread_role::name::scheduler_manual);
		run ();
	} };
}

void celerix::scheduler::manual::stop ()
{
	{
		celerix::lock_guard<celerix::mutex> lock{ mutex };
		stopped = true;
	}
	notify ();
	celerix::join_or_pass (thread);
}

void celerix::scheduler::manual::notify ()
{
	condition.notify_all ();
}

void celerix::scheduler::manual::push (std::shared_ptr<celerix::block> const & block_a, boost::optional<celerix::uint128_t> const & previous_balance_a)
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	queue.push_back (std::make_tuple (block_a, previous_balance_a, celerix::election_behavior::manual));
	notify ();
}

bool celerix::scheduler::manual::contains (celerix::block_hash const & hash) const
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	return std::any_of (queue.cbegin (), queue.cend (), [&hash] (auto const & item) {
		return std::get<0> (item)->hash () == hash;
	});
}

bool celerix::scheduler::manual::predicate () const
{
	return !queue.empty ();
}

void celerix::scheduler::manual::run ()
{
	celerix::unique_lock<celerix::mutex> lock{ mutex };
	while (!stopped)
	{
		condition.wait (lock, [this] () {
			return stopped || predicate ();
		});
		debug_assert ((std::this_thread::yield (), true)); // Introduce some random delay in debug builds
		if (!stopped)
		{
			node.stats.inc (celerix::stat::type::election_scheduler, celerix::stat::detail::loop);

			if (predicate ())
			{
				auto const [block, previous_balance, election_behavior] = queue.front ();
				queue.pop_front ();
				lock.unlock ();
				node.stats.inc (celerix::stat::type::election_scheduler, celerix::stat::detail::insert_manual);
				auto result = node.active.insert (block, election_behavior);
				if (result.election != nullptr)
				{
					result.election->transition_active ();
				}
			}
			else
			{
				lock.unlock ();
			}
			notify ();
			lock.lock ();
		}
	}
}

celerix::container_info celerix::scheduler::manual::container_info () const
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };

	celerix::container_info info;
	info.put ("queue", queue);
	return info;
}