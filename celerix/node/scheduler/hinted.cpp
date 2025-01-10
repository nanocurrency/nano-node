#include <celerix/lib/stats.hpp>
#include <celerix/lib/tomlconfig.hpp>
#include <celerix/node/active_elections.hpp>
#include <celerix/node/election_behavior.hpp>
#include <celerix/node/node.hpp>
#include <celerix/node/online_reps.hpp>
#include <celerix/node/scheduler/hinted.hpp>
#include <celerix/secure/ledger.hpp>
#include <celerix/secure/ledger_set_any.hpp>

/*
 * hinted
 */

celerix::scheduler::hinted::hinted (hinted_config const & config_a, celerix::node & node_a, celerix::vote_cache & vote_cache_a, celerix::active_elections & active_a, celerix::online_reps & online_reps_a, celerix::stats & stats_a) :
	config{ config_a },
	node{ node_a },
	vote_cache{ vote_cache_a },
	active{ active_a },
	online_reps{ online_reps_a },
	stats{ stats_a }
{
}

celerix::scheduler::hinted::~hinted ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void celerix::scheduler::hinted::start ()
{
	debug_assert (!thread.joinable ());

	if (!config.enable)
	{
		return;
	}

	thread = std::thread{ [this] () {
		celerix::thread_role::set (celerix::thread_role::name::scheduler_hinted);
		run ();
	} };
}

void celerix::scheduler::hinted::stop ()
{
	{
		celerix::lock_guard<celerix::mutex> lock{ mutex };
		stopped = true;
	}
	notify ();
	celerix::join_or_pass (thread);
}

void celerix::scheduler::hinted::notify ()
{
	// Avoid notifying when there is very little space inside AEC
	auto const limit = active.limit (celerix::election_behavior::hinted);
	if (active.vacancy (celerix::election_behavior::hinted) >= (limit * config.vacancy_threshold_percent / 100))
	{
		condition.notify_all ();
	}
}

bool celerix::scheduler::hinted::predicate () const
{
	// Check if there is space inside AEC for a new hinted election
	return active.vacancy (celerix::election_behavior::hinted) > 0;
}

void celerix::scheduler::hinted::activate (secure::read_transaction & transaction, celerix::block_hash const & hash, bool check_dependents)
{
	const int max_iterations = 64;

	std::set<celerix::block_hash> visited;
	std::stack<celerix::block_hash> stack;
	stack.push (hash);

	int iterations = 0;
	while (!stack.empty () && iterations++ < max_iterations)
	{
		transaction.refresh_if_needed ();

		const celerix::block_hash current_hash = stack.top ();
		stack.pop ();

		// Check if block exists
		if (auto block = node.ledger.any.block_get (transaction, current_hash); block)
		{
			// Ensure block is not already confirmed
			if (node.block_confirmed_or_being_confirmed (transaction, current_hash))
			{
				stats.inc (celerix::stat::type::hinting, celerix::stat::detail::already_confirmed);
				vote_cache.erase (current_hash); // Remove from vote cache
				continue; // Move on to the next item in the stack
			}

			if (check_dependents)
			{
				// Perform a depth-first search of the dependency graph
				if (!node.ledger.dependents_confirmed (transaction, *block))
				{
					stats.inc (celerix::stat::type::hinting, celerix::stat::detail::dependent_unconfirmed);
					auto dependents = node.ledger.dependent_blocks (transaction, *block);
					for (const auto & dependent_hash : dependents)
					{
						if (!dependent_hash.is_zero () && visited.insert (dependent_hash).second) // Avoid visiting the same block twice
						{
							stack.push (dependent_hash); // Add dependent block to the stack
						}
					}
					continue; // Move on to the next item in the stack
				}
			}

			// Try to insert it into AEC as hinted election
			auto result = node.active.insert (block, celerix::election_behavior::hinted);
			stats.inc (celerix::stat::type::hinting, result.inserted ? celerix::stat::detail::insert : celerix::stat::detail::insert_failed);
		}
		else
		{
			stats.inc (celerix::stat::type::hinting, celerix::stat::detail::missing_block);

			// TODO: Block is missing, bootstrap it
		}
	}
}

void celerix::scheduler::hinted::run_iterative ()
{
	const auto minimum_tally = tally_threshold ();
	const auto minimum_final_tally = final_tally_threshold ();

	// Get the list before db transaction starts to avoid unnecessary slowdowns
	auto tops = vote_cache.top (minimum_tally);

	auto transaction = node.ledger.tx_begin_read ();

	for (auto const & entry : tops)
	{
		if (stopped)
		{
			return;
		}

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
			stats.inc (celerix::stat::type::hinting, celerix::stat::detail::activate);
			activate (transaction, entry.hash, /* activate dependents */ true);
		}
		else
		{
			// Blocks with a vote tally higher than quorum, can be activated and confirmed immediately
			stats.inc (celerix::stat::type::hinting, celerix::stat::detail::activate_immediate);
			activate (transaction, entry.hash, false);
		}
	}
}

void celerix::scheduler::hinted::run ()
{
	celerix::unique_lock<celerix::mutex> lock{ mutex };
	while (!stopped)
	{
		stats.inc (celerix::stat::type::hinting, celerix::stat::detail::loop);

		condition.wait_for (lock, config.check_interval);

		debug_assert ((std::this_thread::yield (), true)); // Introduce some random delay in debug builds

		if (!stopped)
		{
			lock.unlock ();

			if (predicate ())
			{
				run_iterative ();
			}

			lock.lock ();
		}
	}
}

celerix::uint128_t celerix::scheduler::hinted::tally_threshold () const
{
	auto min_tally = (online_reps.trended () / 100) * config.hinting_threshold_percent;
	return min_tally;
}

celerix::uint128_t celerix::scheduler::hinted::final_tally_threshold () const
{
	auto quorum = online_reps.delta ();
	return quorum;
}

bool celerix::scheduler::hinted::cooldown (const celerix::block_hash & hash)
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };

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

celerix::container_info celerix::scheduler::hinted::container_info () const
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };

	celerix::container_info info;
	info.put ("cooldowns", cooldowns_m);
	return info;
}

/*
 * hinted_config
 */

celerix::scheduler::hinted_config::hinted_config (celerix::network_constants const & network)
{
	if (network.is_dev_network ())
	{
		check_interval = std::chrono::milliseconds{ 100 };
		block_cooldown = std::chrono::milliseconds{ 100 };
	}
}

celerix::error celerix::scheduler::hinted_config::serialize (celerix::tomlconfig & toml) const
{
	toml.put ("enable", enable, "Enable or disable hinted elections\ntype:bool");
	toml.put ("hinting_threshold", hinting_threshold_percent, "Percentage of online weight needed to start a hinted election. \ntype:uint32,[0,100]");
	toml.put ("check_interval", check_interval.count (), "Interval between scans of the vote cache for possible hinted elections. \ntype:milliseconds");
	toml.put ("block_cooldown", block_cooldown.count (), "Cooldown period for blocks that failed to start an election. \ntype:milliseconds");
	toml.put ("vacancy_threshold", vacancy_threshold_percent, "Percentage of available space in the active elections container needed to trigger a scan for hinted elections (before the check interval elapses). \ntype:uint32,[0,100]");

	return toml.get_error ();
}

celerix::error celerix::scheduler::hinted_config::deserialize (celerix::tomlconfig & toml)
{
	toml.get ("enable", enable);
	toml.get ("hinting_threshold", hinting_threshold_percent);

	auto check_interval_l = check_interval.count ();
	toml.get ("check_interval", check_interval_l);
	check_interval = std::chrono::milliseconds{ check_interval_l };

	auto block_cooldown_l = block_cooldown.count ();
	toml.get ("block_cooldown", block_cooldown_l);
	block_cooldown = std::chrono::milliseconds{ block_cooldown_l };

	toml.get ("vacancy_threshold", vacancy_threshold_percent);

	if (hinting_threshold_percent > 100)
	{
		toml.get_error ().set ("hinting_threshold must be a number between 0 and 100");
	}
	if (vacancy_threshold_percent > 100)
	{
		toml.get_error ().set ("vacancy_threshold must be a number between 0 and 100");
	}

	return toml.get_error ();
}
