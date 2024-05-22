#include <nano/lib/blocks.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/election.hpp>
#include <nano/node/node.hpp>
#include <nano/node/scheduler/buckets.hpp>
#include <nano/node/scheduler/priority.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/secure/ledger_set_confirmed.hpp>

nano::scheduler::priority::priority (nano::node & node_a, nano::stats & stats_a) :
	config{ node_a.config.priority_scheduler },
	node{ node_a },
	stats{ stats_a },
	buckets{ std::make_unique<scheduler::buckets> () }
{
}

nano::scheduler::priority::~priority ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void nano::scheduler::priority::start ()
{
	debug_assert (!thread.joinable ());

	if (!config.enabled)
	{
		return;
	}

	thread = std::thread{ [this] () {
		nano::thread_role::set (nano::thread_role::name::scheduler_priority);
		run ();
	} };
}

void nano::scheduler::priority::stop ()
{
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		stopped = true;
	}
	notify ();
	nano::join_or_pass (thread);
}

bool nano::scheduler::priority::activate (secure::transaction const & transaction, nano::account const & account)
{
	debug_assert (!account.is_zero ());
	auto info = node.ledger.any.account_get (transaction, account);
	if (info)
	{
		nano::confirmation_height_info conf_info;
		node.store.confirmation_height.get (transaction, account, conf_info);
		if (conf_info.height < info->block_count)
		{
			return activate (transaction, account, *info, conf_info);
		}
	}

	stats.inc (nano::stat::type::election_scheduler, nano::stat::detail::activate_skip);
	return false; // Not activated
}

bool nano::scheduler::priority::activate (secure::transaction const & transaction, nano::account const & account, nano::account_info const & info, nano::confirmation_height_info const & conf_info)
{
	debug_assert (conf_info.frontier != info.head);

	auto hash = conf_info.height == 0 ? info.open_block : node.ledger.any.block_successor (transaction, conf_info.frontier).value_or (0);
	auto block = node.ledger.any.block_get (transaction, hash);
	release_assert (block != nullptr);

	if (node.ledger.dependents_confirmed (transaction, *block))
	{
		auto const balance = node.ledger.any.block_balance (transaction, hash).value ();
		auto const previous_balance = node.ledger.any.block_balance (transaction, conf_info.frontier).value_or (0);
		auto const balance_priority = std::max (balance, previous_balance);

		node.stats.inc (nano::stat::type::election_scheduler, nano::stat::detail::activated);
		node.logger.trace (nano::log::type::election_scheduler, nano::log::detail::block_activated,
		nano::log::arg{ "account", account.to_account () }, // TODO: Convert to lazy eval
		nano::log::arg{ "block", block },
		nano::log::arg{ "time", info.modified },
		nano::log::arg{ "priority", balance_priority });

		{
			nano::lock_guard<nano::mutex> lock{ mutex };
			buckets->push (info.modified, block, balance_priority);
		}
		notify ();

		return true; // Activated
	}

	stats.inc (nano::stat::type::election_scheduler, nano::stat::detail::activate_failed);
	return false; // Not activated
}

void nano::scheduler::priority::notify ()
{
	condition.notify_all ();
}

std::size_t nano::scheduler::priority::size () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return buckets->size ();
}

bool nano::scheduler::priority::empty_locked () const
{
	return buckets->empty ();
}

bool nano::scheduler::priority::empty () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return empty_locked ();
}

bool nano::scheduler::priority::predicate () const
{
	return node.active.vacancy (nano::election_behavior::priority) > 0 && !buckets->empty ();
}

void nano::scheduler::priority::run ()
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
			stats.inc (nano::stat::type::election_scheduler, nano::stat::detail::loop);

			if (predicate ())
			{
				auto block = buckets->top ();
				buckets->pop ();
				lock.unlock ();
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

std::unique_ptr<nano::container_info_component> nano::scheduler::priority::collect_container_info (std::string const & name)
{
	nano::unique_lock<nano::mutex> lock{ mutex };

	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (buckets->collect_container_info ("buckets"));
	return composite;
}
