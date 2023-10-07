#include <nano/lib/stats.hpp>
#include <nano/lib/tomlconfig.hpp>
#include <nano/node/node.hpp>
#include <nano/node/scheduler/hinted.hpp>

/*
 * hinted
 */

nano::scheduler::hinted::hinted (hinted_config const & config_a, nano::node & node_a, nano::vote_cache & vote_cache_a, nano::active_transactions & active_a, nano::online_reps & online_reps_a, nano::stats & stats_a) :
	config{ config_a },
	node{ node_a },
	vote_cache{ vote_cache_a },
	active{ active_a },
	online_reps{ online_reps_a },
	stats{ stats_a }
{
}

nano::scheduler::hinted::~hinted ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void nano::scheduler::hinted::start ()
{
	debug_assert (!thread.joinable ());

	thread = std::thread{ [this] () {
		nano::thread_role::set (nano::thread_role::name::scheduler_hinted);
		run ();
	} };
}

void nano::scheduler::hinted::stop ()
{
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		stopped = true;
	}
	notify ();
	nano::join_or_pass (thread);
}

void nano::scheduler::hinted::notify ()
{
	condition.notify_all ();
}

bool nano::scheduler::hinted::predicate () const
{
	// Check if there is space inside AEC for a new hinted election
	return active.vacancy (nano::election_behavior::hinted) > 0;
}

void nano::scheduler::hinted::activate (const nano::store::transaction & transaction, const nano::block_hash & hash, bool check_dependents)
{
	std::stack<nano::block_hash> stack;
	stack.push (hash);

	while (!stack.empty ())
	{
		const nano::block_hash current_hash = stack.top ();
		stack.pop ();

		// Check if block exists
		if (auto block = node.store.block.get (transaction, current_hash); block)
		{
			// Ensure block is not already confirmed
			if (node.block_confirmed_or_being_confirmed (transaction, current_hash))
			{
				stats.inc (nano::stat::type::hinting, nano::stat::detail::already_confirmed);
				continue; // Move on to the next item in the stack
			}

			if (check_dependents)
			{
				// Perform a depth-first search of the dependency graph
				if (!node.ledger.dependents_confirmed (transaction, *block))
				{
					stats.inc (nano::stat::type::hinting, nano::stat::detail::dependent_unconfirmed);
					auto dependents = node.ledger.dependent_blocks (transaction, *block);
					for (const auto & dependent_hash : dependents)
					{
						if (!dependent_hash.is_zero ())
						{
							stack.push (dependent_hash); // Add dependent block to the stack
						}
					}
					continue; // Move on to the next item in the stack
				}
			}

			// Try to insert it into AEC as hinted election
			auto result = node.active.insert (block, nano::election_behavior::hinted);
			stats.inc (nano::stat::type::hinting, result.inserted ? nano::stat::detail::insert : nano::stat::detail::insert_failed);
		}
		else
		{
			stats.inc (nano::stat::type::hinting, nano::stat::detail::missing_block);
			node.bootstrap_block (current_hash);
		}
	}
}

void nano::scheduler::hinted::run_iterative ()
{
	const auto minimum_tally = tally_threshold ();
	const auto minimum_final_tally = final_tally_threshold ();

	auto transaction = node.store.tx_begin_read ();

	for (auto const & entry : vote_cache.top (minimum_tally))
	{
		if (!predicate ())
		{
			return;
		}

		if (cooldown (entry.hash))
		{
			continue;
		}

		// Check dependents only if cached tally is lower than quorum
		if (entry.final_tally < minimum_final_tally)
		{
			// Ensure all dependent blocks are already confirmed before activating
			stats.inc (nano::stat::type::hinting, nano::stat::detail::activate);
			activate (transaction, entry.hash, /* activate dependents */ true);
		}
		else
		{
			// Blocks with a vote tally higher than quorum, can be activated and confirmed immediately
			stats.inc (nano::stat::type::hinting, nano::stat::detail::activate_immediate);
			activate (transaction, entry.hash, false);
		}
	}
}

void nano::scheduler::hinted::run ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		stats.inc (nano::stat::type::hinting, nano::stat::detail::loop);

		condition.wait_for (lock, config.check_interval);

		debug_assert ((std::this_thread::yield (), true)); // Introduce some random delay in debug builds

		if (!stopped)
		{
			if (predicate ())
			{
				run_iterative ();
			}
		}
	}
}

nano::uint128_t nano::scheduler::hinted::tally_threshold () const
{
	auto min_tally = (online_reps.trended () / 100) * config.hinting_threshold_percent;
	return min_tally;
}

nano::uint128_t nano::scheduler::hinted::final_tally_threshold () const
{
	auto quorum = online_reps.delta ();
	return quorum;
}

bool nano::scheduler::hinted::cooldown (const nano::block_hash & hash)
{
	auto const now = std::chrono::steady_clock::now ();

	// Check if the hash is still in the cooldown period using the hashed index
	auto const & hashed_index = cooldowns_m.get<tag_hash> ();
	if (auto it = hashed_index.find (hash); it != hashed_index.end ())
	{
		if (it->timeout > now)
		{
			return true; // Needs cooldown
		}
		cooldowns_m.erase (it); // Entry is outdated, so remove it
	}

	// Insert the new entry
	cooldowns_m.insert ({ hash, now + config.block_cooldown });

	// Trim old entries
	auto & seq_index = cooldowns_m.get<tag_timeout> ();
	while (!seq_index.empty () && seq_index.begin ()->timeout <= now)
	{
		seq_index.erase (seq_index.begin ());
	}

	return false; // No need to cooldown
}

/*
 * hinted_config
 */

nano::scheduler::hinted_config::hinted_config (nano::network_constants const & network)
{
	if (network.is_dev_network ())
	{
		check_interval = std::chrono::milliseconds{ 100 };
	}
}

nano::error nano::scheduler::hinted_config::serialize (nano::tomlconfig & toml) const
{
	toml.put ("hinting_threshold", hinting_threshold_percent, "Percentage of online weight needed to start a hinted election. \ntype:uint32,[0,100]");
	toml.put ("check_interval", check_interval.count (), "Interval between scans of the vote cache for possible hinted elections. \ntype:milliseconds");
	toml.put ("block_cooldown", block_cooldown.count (), "Cooldown period for blocks that failed to start an election. \ntype:milliseconds");

	return toml.get_error ();
}

nano::error nano::scheduler::hinted_config::deserialize (nano::tomlconfig & toml)
{
	toml.get ("hinting_threshold", hinting_threshold_percent);

	auto check_interval_l = check_interval.count ();
	toml.get ("check_interval", check_interval_l);
	check_interval = std::chrono::milliseconds{ check_interval_l };

	auto block_cooldown_l = block_cooldown.count ();
	toml.get ("block_cooldown", block_cooldown_l);
	block_cooldown = std::chrono::milliseconds{ block_cooldown_l };

	if (hinting_threshold_percent > 100)
	{
		toml.get_error ().set ("hinting_threshold must be a number between 0 and 100");
	}

	return toml.get_error ();
}