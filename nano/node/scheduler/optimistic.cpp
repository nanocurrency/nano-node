#include <nano/lib/blocks.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/tomlconfig.hpp>
#include <nano/node/active_transactions.hpp>
#include <nano/node/election_behavior.hpp>
#include <nano/node/node.hpp>
#include <nano/node/scheduler/optimistic.hpp>
#include <nano/secure/ledger.hpp>

nano::scheduler::optimistic::optimistic (optimistic_config const & config_a, nano::node & node_a, nano::ledger & ledger_a, nano::active_transactions & active_a, nano::network_constants const & network_constants_a, nano::stats & stats_a) :
	config{ config_a },
	node{ node_a },
	ledger{ ledger_a },
	active{ active_a },
	network_constants{ network_constants_a },
	stats{ stats_a }
{
}

nano::scheduler::optimistic::~optimistic ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void nano::scheduler::optimistic::start ()
{
	debug_assert (!thread.joinable ());

	if (!config.enabled)
	{
		return;
	}

	thread = std::thread{ [this] () {
		nano::thread_role::set (nano::thread_role::name::scheduler_optimistic);
		run ();
	} };
}

void nano::scheduler::optimistic::stop ()
{
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		stopped = true;
	}
	notify ();
	nano::join_or_pass (thread);
}

void nano::scheduler::optimistic::notify ()
{
	condition.notify_all ();
}

bool nano::scheduler::optimistic::activate_predicate (const nano::account_info & account_info, const nano::confirmation_height_info & conf_info) const
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

bool nano::scheduler::optimistic::activate (const nano::account & account, const nano::account_info & account_info, const nano::confirmation_height_info & conf_info)
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

			stats.inc (nano::stat::type::optimistic_scheduler, nano::stat::detail::activated);
			candidates.push_back ({ account, nano::clock::now () });
		}
		return true; // Activated
	}
	return false; // Not activated
}

bool nano::scheduler::optimistic::predicate () const
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
	bool result = nano::elapsed (candidate.timestamp, network_constants.optimistic_activation_delay);
	return result;
}

void nano::scheduler::optimistic::run ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		stats.inc (nano::stat::type::optimistic_scheduler, nano::stat::detail::loop);

		if (predicate ())
		{
			while (predicate ())
			{
				debug_assert (!candidates.empty ());
				auto candidate = candidates.front ();
				candidates.pop_front ();

				lock.unlock ();

				run_one (ledger.store.tx_begin_read (), candidate);

				lock.lock ();
			}
		}

		condition.wait_for (lock, network_constants.optimistic_activation_delay / 2, [this] () {
			return stopped || predicate ();
		});
	}
}

void nano::scheduler::optimistic::run_one (store::transaction const & transaction, entry const & candidate)
{
	auto block = ledger.head_block (transaction, candidate.account);
	if (block)
	{
		// Ensure block is not already confirmed
		if (!node.block_confirmed_or_being_confirmed (transaction, block->hash ()))
		{
			// Try to insert it into AEC
			// We check for AEC vacancy inside our predicate
			auto result = node.active.insert (block, nano::election_behavior::optimistic);

			stats.inc (nano::stat::type::optimistic_scheduler, result.inserted ? nano::stat::detail::insert : nano::stat::detail::insert_failed);
		}
	}
}

std::unique_ptr<nano::container_info_component> nano::scheduler::optimistic::collect_container_info (const std::string & name) const
{
	nano::lock_guard<nano::mutex> guard{ mutex };

	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "candidates", candidates.size (), sizeof (decltype (candidates)::value_type) }));
	return composite;
}

/*
 * optimistic_scheduler_config
 */

nano::error nano::scheduler::optimistic_config::deserialize (nano::tomlconfig & toml)
{
	toml.get ("enabled", enabled);
	toml.get ("gap_threshold", gap_threshold);
	toml.get ("max_size", max_size);

	return toml.get_error ();
}

nano::error nano::scheduler::optimistic_config::serialize (nano::tomlconfig & toml) const
{
	toml.put ("enable", enabled, "Enable or disable optimistic elections\ntype:bool");
	toml.put ("gap_threshold", gap_threshold, "Minimum difference between confirmation frontier and account frontier to become a candidate for optimistic confirmation\ntype:uint64");
	toml.put ("max_size", max_size, "Maximum number of candidates stored in memory\ntype:uint64");

	return toml.get_error ();
}
