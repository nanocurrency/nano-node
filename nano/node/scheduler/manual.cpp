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
	notify ();
	nano::join_or_pass (thread);
}

void nano::scheduler::manual::notify ()
{
	condition.notify_all ();
}

void nano::scheduler::manual::push (std::shared_ptr<nano::block> const & block_a, boost::optional<nano::uint128_t> const & previous_balance_a, nano::election_behavior election_behavior_a)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	queue.push_back (std::make_tuple (block_a, previous_balance_a, election_behavior_a));
	notify ();
}

bool nano::scheduler::manual::predicate () const
{
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
		debug_assert ((std::this_thread::yield (), true)); // Introduce some random delay in debug builds
		if (!stopped)
		{
			node.stats.inc (nano::stat::type::election_scheduler, nano::stat::detail::loop);

			if (predicate ())
			{
				auto const [block, previous_balance, election_behavior] = queue.front ();
				queue.pop_front ();
				lock.unlock ();
				node.stats.inc (nano::stat::type::election_scheduler, nano::stat::detail::insert_manual);
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

std::unique_ptr<nano::container_info_component> nano::scheduler::manual::collect_container_info (std::string const & name) const
{
	nano::unique_lock<nano::mutex> lock{ mutex };

	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "queue", queue.size (), sizeof (decltype (queue)::value_type) }));
	return composite;
}
