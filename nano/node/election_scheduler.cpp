#include <nano/node/election_scheduler.hpp>

#include <nano/node/node.hpp>

nano::election_scheduler::election_scheduler (nano::node & node) :
	node{ node },
	stopped{ false },
	thread{ [this] () { run (); }}
{
}

nano::election_scheduler::~election_scheduler ()
{
	stop ();
	thread.join ();
}

void nano::election_scheduler::insert (std::shared_ptr<nano::block> const & block_a, boost::optional<nano::uint128_t> const & previous_balance_a, nano::election_behavior election_behavior_a, std::function<void(std::shared_ptr<nano::block> const &)> const & confirmation_action_a)
{
	nano::unique_lock<nano::mutex> lock (node.active.mutex);
	node.active.insert_impl (lock, block_a, previous_balance_a, election_behavior_a, confirmation_action_a);
}

void nano::election_scheduler::activate (nano::account const & account_a)
{
	auto transaction (node.store.tx_begin_read ());
	nano::account_info account_info;
	if (!node.store.account_get (transaction, account_a, account_info))
	{
		nano::confirmation_height_info conf_info;
		node.store.confirmation_height_get (transaction, account_a, conf_info);
		if (conf_info.height < account_info.block_count)
		{
			debug_assert (conf_info.frontier != account_info.head);
			auto hash = conf_info.height == 0 ? account_info.open_block : node.store.block_successor (transaction, conf_info.frontier);
			auto block = node.store.block_get (transaction, hash);
			release_assert (block != nullptr);
			if (node.ledger.dependents_confirmed (transaction, *block))
			{
				std::lock_guard<std::mutex> lock{ mutex };
				activate_queue.push_back (block);
				condition.notify_all ();
			}
		}
	}
}

void nano::election_scheduler::stop ()
{
	std::unique_lock<std::mutex> lock{ mutex };
	stopped = true;
	condition.notify_all ();
}

void nano::election_scheduler::flush ()
{
	std::unique_lock<std::mutex> lock{ mutex };
	auto activate_target = activate_queued + activate_queue.size ();
	condition.wait (lock, [this, &activate_target] () { return activate_queued >= activate_target; });
}

void nano::election_scheduler::run ()
{
	std::unique_lock<std::mutex> lock{ mutex };
	while (!stopped)
	{
		condition.wait (lock, [this] () {
			return stopped || !activate_queue.empty ();
		});
		if (!stopped)
		{
			if (!activate_queue.empty())
			{
				auto block = activate_queue.front ();
				activate_queue.pop_front ();
				++activate_queued;
				insert (block);
				auto election = node.active.election (block->qualified_root ());
				if (election != nullptr)
				{
					election->transition_active ();
				}
			}
			condition.notify_all ();
		}
	}
}
