#include <nano/node/election_scheduler.hpp>
#include <nano/node/node.hpp>

nano::election_scheduler::election_scheduler (nano::node & node_a, nano::stats & stats_a) :
	node{ node_a },
	stats{ stats_a }
{
}

nano::election_scheduler::~election_scheduler ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void nano::election_scheduler::start ()
{
	debug_assert (!thread.joinable ());

	thread = std::thread{ [this] () {
		nano::thread_role::set (nano::thread_role::name::election_scheduler);
		run ();
	} };
}

void nano::election_scheduler::stop ()
{
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		stopped = true;
	}
	notify ();
	nano::join_or_pass (thread);
}

void nano::election_scheduler::manual (std::shared_ptr<nano::block> const & block_a, boost::optional<nano::uint128_t> const & previous_balance_a, nano::election_behavior election_behavior_a)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	manual_queue.push_back (std::make_tuple (block_a, previous_balance_a, election_behavior_a));
	notify ();
}

bool nano::election_scheduler::activate (nano::account const & account_a, nano::transaction const & transaction)
{
	debug_assert (!account_a.is_zero ());
	auto info = node.ledger.account_info (transaction, account_a);
	if (info)
	{
		nano::confirmation_height_info conf_info;
		node.store.confirmation_height.get (transaction, account_a, conf_info);
		if (conf_info.height < info->block_count)
		{
			debug_assert (conf_info.frontier != info->head);
			auto hash = conf_info.height == 0 ? info->open_block : node.store.block.successor (transaction, conf_info.frontier);
			auto block = node.store.block.get (transaction, hash);
			debug_assert (block != nullptr);
			if (node.ledger.dependents_confirmed (transaction, *block))
			{
				stats.inc (nano::stat::type::election_scheduler, nano::stat::detail::activated);
				auto balance = node.ledger.balance (transaction, hash);
				auto previous_balance = node.ledger.balance (transaction, conf_info.frontier);
				nano::lock_guard<nano::mutex> lock{ mutex };
				priority.push (info->modified, block, std::max (balance, previous_balance));
				notify ();
				return true; // Activated
			}
		}
	}
	return false; // Not activated
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
	/*
	 * Both normal and hinted election schedulers are well-behaved, meaning they first check for AEC vacancy before inserting new elections.
	 * However, it is possible that AEC will be temporarily overfilled in case it's running at full capacity and election hinting or manual queue kicks in.
	 * That case will lead to unwanted churning of elections, so this allows for AEC to be overfilled to 125% until erasing of elections happens.
	 */
	return node.active.vacancy () < -(node.active.limit () / 4);
}

void nano::election_scheduler::run ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		condition.wait (lock, [this] () {
			return stopped || priority_queue_predicate () || manual_queue_predicate () || overfill_predicate ();
		});
		debug_assert ((std::this_thread::yield (), true)); // Introduce some random delay in debug builds
		if (!stopped)
		{
			stats.inc (nano::stat::type::election_scheduler, nano::stat::detail::loop);

			if (overfill_predicate ())
			{
				lock.unlock ();
				stats.inc (nano::stat::type::election_scheduler, nano::stat::detail::erase_oldest);
				node.active.erase_oldest ();
			}
			else if (manual_queue_predicate ())
			{
				auto const [block, previous_balance, election_behavior] = manual_queue.front ();
				manual_queue.pop_front ();
				lock.unlock ();
				stats.inc (nano::stat::type::election_scheduler, nano::stat::detail::insert_manual);
				node.active.insert (block, election_behavior);
			}
			else if (priority_queue_predicate ())
			{
				auto block = priority.top ();
				priority.pop ();
				lock.unlock ();
				std::shared_ptr<nano::election> election;
				stats.inc (nano::stat::type::election_scheduler, nano::stat::detail::insert_priority);
				auto result = node.active.insert (block);
				if (result.inserted)
				{
					stats.inc (nano::stat::type::election_scheduler, nano::stat::detail::insert_priority_success);
				}
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

std::unique_ptr<nano::container_info_component> nano::election_scheduler::collect_container_info (std::string const & name)
{
	nano::unique_lock<nano::mutex> lock{ mutex };

	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "manual_queue", manual_queue.size (), sizeof (decltype (manual_queue)::value_type) }));
	composite->add_component (priority.collect_container_info ("priority"));
	return composite;
}
