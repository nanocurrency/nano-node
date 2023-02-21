#include <nano/lib/stats.hpp>
#include <nano/lib/tomlconfig.hpp>
#include <nano/node/node.hpp>
#include <nano/node/optimistic_scheduler.hpp>

nano::optimistic_scheduler::optimistic_scheduler (optimistic_scheduler_config const & config_a, nano::node & node_a, nano::ledger & ledger_a, nano::active_transactions & active_a, nano::stats & stats_a) :
	config{ config_a },
	node{ node_a },
	ledger{ ledger_a },
	active{ active_a },
	stats{ stats_a }
{
}

nano::optimistic_scheduler::~optimistic_scheduler ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void nano::optimistic_scheduler::start ()
{
	if (!config.enabled)
	{
		return;
	}

	debug_assert (!thread.joinable ());

	thread = std::thread{ [this] () {
		nano::thread_role::set (nano::thread_role::name::optimistic_scheduler);
		run ();
	} };
}

void nano::optimistic_scheduler::stop ()
{
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		stopped = true;
	}
	notify ();
	nano::join_or_pass (thread);
}

void nano::optimistic_scheduler::notify ()
{
	condition.notify_all ();
}

bool nano::optimistic_scheduler::activate_predicate (const nano::account_info & account_info, const nano::confirmation_height_info & conf_info) const
{
	// Chain with a big enough gap between account frontier and confirmation frontier
	if (account_info.block_count - conf_info.height > config.gap_threshold)
	{
		return true;
	}
	// Account with nothing confirmed yet
	if (conf_info.height == 0)
	{
		return true;
	}
	return false;
}

bool nano::optimistic_scheduler::activate (const nano::account & account, const nano::account_info & account_info, const nano::confirmation_height_info & conf_info)
{
	if (!config.enabled)
	{
		return false;
	}

	debug_assert (account_info.block_count >= conf_info.height);
	if (activate_predicate (account_info, conf_info))
	{
		{
			nano::lock_guard<nano::mutex> lock{ mutex };

			// Prevent duplicate candidate accounts
			if (candidates.get<tag_account> ().contains (account))
			{
				return false; // Not activated
			}
			// Limit candidates container size
			if (candidates.size () >= config.max_size)
			{
				return false; // Not activated
			}

			stats.inc (nano::stat::type::optimistic, nano::stat::detail::activated);
			candidates.push_back ({ account, nano::clock::now () });
		}
		notify ();
		return true; // Activated
	}
	return false; // Not activated
}

bool nano::optimistic_scheduler::predicate () const
{
	debug_assert (!mutex.try_lock ());

	if (active.vacancy (nano::election_behavior::optimistic) <= 0)
	{
		return false;
	}
	if (candidates.empty ())
	{
		return false;
	}

	auto candidate = candidates.front ();
	bool result = nano::elapsed (candidate.timestamp, std::chrono::seconds{ 5 });
	return result;
}

void nano::optimistic_scheduler::run ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		stats.inc (nano::stat::type::optimistic, nano::stat::detail::loop);

		if (predicate ())
		{
			auto transaction = ledger.store.tx_begin_read ();

			while (predicate ())
			{
				debug_assert (!candidates.empty ());
				auto candidate = candidates.front ();
				candidates.pop_front ();
				lock.unlock ();

				run_one (transaction, candidate);

				lock.lock ();
			}
		}

		condition.wait (lock, [this] () {
			return stopped || predicate ();
		});
	}
}

void nano::optimistic_scheduler::run_one (nano::transaction const & transaction, entry const & candidate)
{
	auto block = ledger.head_block (transaction, candidate.account);
	if (block)
	{
		// Ensure block is not already confirmed
		if (!node.block_confirmed_or_being_confirmed (block->hash ()))
		{
			// Try to insert it into AEC
			// We check for AEC vacancy inside our predicate
			auto result = node.active.insert (block, nano::election_behavior::optimistic);

			stats.inc (nano::stat::type::optimistic, result.inserted ? nano::stat::detail::insert : nano::stat::detail::insert_failed);
		}
	}
}

/*
 * optimistic_scheduler_config
 */

nano::error nano::optimistic_scheduler_config::deserialize (nano::tomlconfig & toml)
{
	toml.get ("enabled", enabled);
	toml.get ("gap_threshold", gap_threshold);
	toml.get ("max_size", max_size);

	return toml.get_error ();
}

nano::error nano::optimistic_scheduler_config::serialize (nano::tomlconfig & toml) const
{
	toml.put ("enable", enabled, "Enable or disable optimistic elections\ntype:bool");
	toml.put ("gap_threshold", gap_threshold, "Minimum difference between confirmation frontier and account frontier to become a candidate for optimistic confirmation\ntype:uint64");
	toml.put ("max_size", max_size, "Maximum number of candidates stored in memory\ntype:uint64");

	return toml.get_error ();
}