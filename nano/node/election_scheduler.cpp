#include <nano/node/election_scheduler.hpp>
#include <nano/node/node.hpp>

nano::election_scheduler::election_scheduler (nano::node & node) :
	node{ node },
	stopped{ false },
	thread{ [this] () { run (); } }
{
}

nano::election_scheduler::~election_scheduler ()
{
	stop ();
	thread.join ();
}

void nano::election_scheduler::manual (std::shared_ptr<nano::block> const & block_a, boost::optional<nano::uint128_t> const & previous_balance_a, nano::election_behavior election_behavior_a, std::function<void (std::shared_ptr<nano::block> const &)> const & confirmation_action_a)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	manual_queue.push_back (std::make_tuple (block_a, previous_balance_a, election_behavior_a, confirmation_action_a));
	notify ();
}

void nano::election_scheduler::activate (nano::account const & account_a, nano::transaction const & transaction)
{
	debug_assert (!account_a.is_zero ());
	nano::account_info account_info;
	if (!node.store.account.get (transaction, account_a, account_info))
	{
		nano::confirmation_height_info conf_info;
		node.store.confirmation_height.get (transaction, account_a, conf_info);
		if (conf_info.height < account_info.block_count)
		{
			debug_assert (conf_info.frontier != account_info.head);
			auto hash = conf_info.height == 0 ? account_info.open_block : node.store.block.successor (transaction, conf_info.frontier);
			auto block = node.store.block.get (transaction, hash);
			debug_assert (block != nullptr);
			if (node.ledger.dependents_confirmed (transaction, *block))
			{
				nano::lock_guard<nano::mutex> lock{ mutex };
				priority.push (account_info.modified, block);
				notify ();
			}
		}
	}
}

void nano::election_scheduler::stop ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	stopped = true;
	notify ();
}

void nano::election_scheduler::flush ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	condition.wait (lock, [this] () {
		return stopped || empty_locked () || node.active.vacancy () <= 0;
	});
}

void nano::election_scheduler::notify ()
{
	condition.notify_all ();
}

std::size_t nano::election_scheduler::size () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return priority.size () + manual_queue.size ();
}

bool nano::election_scheduler::empty_locked () const
{
	return priority.empty () && manual_queue.empty ();
}

bool nano::election_scheduler::empty () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return empty_locked ();
}

std::size_t nano::election_scheduler::priority_queue_size () const
{
	return priority.size ();
}

bool nano::election_scheduler::priority_queue_predicate () const
{
	return node.active.vacancy () > 0 && !priority.empty ();
}

bool nano::election_scheduler::manual_queue_predicate () const
{
	return !manual_queue.empty ();
}

bool nano::election_scheduler::overfill_predicate () const
{
	return node.active.vacancy () < 0;
}

void nano::election_scheduler::run ()
{
	nano::thread_role::set (nano::thread_role::name::election_scheduler);
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		condition.wait (lock, [this] () {
			return stopped || priority_queue_predicate () || manual_queue_predicate () || overfill_predicate ();
		});
		debug_assert ((std::this_thread::yield (), true)); // Introduce some random delay in debug builds
		if (!stopped)
		{
			if (overfill_predicate ())
			{
				node.active.erase_oldest ();
			}
			else if (manual_queue_predicate ())
			{
				auto const [block, previous_balance, election_behavior, confirmation_action] = manual_queue.front ();
				nano::unique_lock<nano::mutex> lock2 (node.active.mutex);
				node.active.insert_impl (lock2, block, previous_balance, election_behavior, confirmation_action);
				manual_queue.pop_front ();
			}
			else if (priority_queue_predicate ())
			{
				auto block = priority.top ();
				std::shared_ptr<nano::election> election;
				nano::unique_lock<nano::mutex> lock2 (node.active.mutex);
				election = node.active.insert_impl (lock2, block).election;
				if (election != nullptr)
				{
					election->transition_active ();
				}
				priority.pop ();
			}
			notify ();
		}
	}
}
