#include <celerix/lib/blocks.hpp>
#include <celerix/lib/stats.hpp>
#include <celerix/lib/tomlconfig.hpp>
#include <celerix/node/active_elections.hpp>
#include <celerix/node/election_behavior.hpp>
#include <celerix/node/node.hpp>
#include <celerix/node/scheduler/optimistic.hpp>
#include <celerix/secure/ledger.hpp>
#include <celerix/secure/ledger_set_any.hpp>
#include <celerix/secure/ledger_set_confirmed.hpp>

celerix::scheduler::optimistic::optimistic (optimistic_config const & config_a, celerix::node & node_a, celerix::ledger & ledger_a, celerix::active_elections & active_a, celerix::network_constants const & network_constants_a, celerix::stats & stats_a) :
	config{ config_a },
	node{ node_a },
	ledger{ ledger_a },
	active{ active_a },
	network_constants{ network_constants_a },
	stats{ stats_a }
{
}

celerix::scheduler::optimistic::~optimistic ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void celerix::scheduler::optimistic::start ()
{
	debug_assert (!thread.joinable ());

	if (!config.enable)
	{
		return;
	}

	thread = std::thread{ [this] () {
		celerix::thread_role::set (celerix::thread_role::name::scheduler_optimistic);
		run ();
	} };
}

void celerix::scheduler::optimistic::stop ()
{
	{
		celerix::lock_guard<celerix::mutex> guard{ mutex };
		stopped = true;
	}
	notify ();
	celerix::join_or_pass (thread);
}

void celerix::scheduler::optimistic::notify ()
{
	condition.notify_all ();
}

bool celerix::scheduler::optimistic::activate_predicate (const celerix::account_info & account_info, const celerix::confirmation_height_info & conf_info) const
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

bool celerix::scheduler::optimistic::activate (const celerix::account & account, const celerix::account_info & account_info, const celerix::confirmation_height_info & conf_info)
{
	if (!config.enable)
	{
		return false;
	}

	debug_assert (account_info.block_count >= conf_info.height);
	if (activate_predicate (account_info, conf_info))
	{
		{
			celerix::lock_guard<celerix::mutex> lock{ mutex };

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

			stats.inc (celerix::stat::type::optimistic_scheduler, celerix::stat::detail::activated);
			candidates.push_back ({ account, celerix::clock::now () });
		}
		return true; // Activated
	}
	return false; // Not activated
}

bool celerix::scheduler::optimistic::predicate () const
{
	debug_assert (!mutex.try_lock ());

	if (active.vacancy (celerix::election_behavior::optimistic) <= 0)
	{
		return false;
	}
	if (candidates.empty ())
	{
		return false;
	}

	auto candidate = candidates.front ();
	bool result = celerix::elapsed (candidate.timestamp, network_constants.optimistic_activation_delay);
	return result;
}

void celerix::scheduler::optimistic::run ()
{
	celerix::unique_lock<celerix::mutex> lock{ mutex };
	while (!stopped)
	{
		stats.inc (celerix::stat::type::optimistic_scheduler, celerix::stat::detail::loop);

		if (predicate ())
		{
			auto transaction = ledger.tx_begin_read ();

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

		condition.wait_for (lock, network_constants.optimistic_activation_delay / 2, [this] () {
			return stopped || predicate ();
		});
	}
}

void celerix::scheduler::optimistic::run_one (secure::transaction const & transaction, entry const & candidate)
{
	auto block = ledger.any.block_get (transaction, ledger.any.account_head (transaction, candidate.account));
	if (block)
	{
		// Ensure block is not already confirmed
		if (!node.block_confirmed_or_being_confirmed (transaction, block->hash ()))
		{
			// Try to insert it into AEC
			// We check for AEC vacancy inside our predicate
			auto result = node.active.insert (block, celerix::election_behavior::optimistic);

			stats.inc (celerix::stat::type::optimistic_scheduler, result.inserted ? celerix::stat::detail::insert : celerix::stat::detail::insert_failed);
		}
	}
}

celerix::container_info celerix::scheduler::optimistic::container_info () const
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };

	celerix::container_info info;
	info.put ("candidates", candidates);
	return info;
}

/*
 * optimistic_scheduler_config
 */

celerix::error celerix::scheduler::optimistic_config::deserialize (celerix::tomlconfig & toml)
{
	toml.get ("enable", enable);
	toml.get ("gap_threshold", gap_threshold);
	toml.get ("max_size", max_size);

	return toml.get_error ();
}

celerix::error celerix::scheduler::optimistic_config::serialize (celerix::tomlconfig & toml) const
{
	toml.put ("enable", enable, "Enable or disable optimistic elections\ntype:bool");
	toml.put ("gap_threshold", gap_threshold, "Minimum difference between confirmation frontier and account frontier to become a candidate for optimistic confirmation\ntype:uint64");
	toml.put ("max_size", max_size, "Maximum number of candidates stored in memory\ntype:uint64");

	return toml.get_error ();
}
