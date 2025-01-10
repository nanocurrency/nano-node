#include <celerix/lib/blocks.hpp>
#include <celerix/node/active_elections.hpp>
#include <celerix/node/election.hpp>
#include <celerix/node/node.hpp>
#include <celerix/node/scheduler/bucket.hpp>

/*
 * bucket
 */

celerix::scheduler::bucket::bucket (celerix::bucket_index index_a, priority_bucket_config const & config_a, celerix::active_elections & active_a, celerix::stats & stats_a) :
	index{ index_a },
	config{ config_a },
	active{ active_a },
	stats{ stats_a }
{
}

celerix::scheduler::bucket::~bucket ()
{
}

bool celerix::scheduler::bucket::available () const
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };

	if (queue.empty ())
	{
		return false;
	}
	else
	{
		return election_vacancy (queue.begin ()->time);
	}
}

bool celerix::scheduler::bucket::election_vacancy (celerix::priority_timestamp candidate) const
{
	debug_assert (!mutex.try_lock ());

	if (elections.size () < config.reserved_elections || elections.size () < config.max_elections)
	{
		return active.vacancy (celerix::election_behavior::priority) > 0;
	}
	if (!elections.empty ())
	{
		auto lowest = elections.get<tag_priority> ().begin ()->priority;

		// Compare to equal to drain duplicates
		if (candidate <= lowest)
		{
			// Bound number of reprioritizations
			return elections.size () < config.max_elections * 2;
		};
	}
	return false;
}

bool celerix::scheduler::bucket::election_overfill () const
{
	debug_assert (!mutex.try_lock ());

	if (elections.size () < config.reserved_elections)
	{
		return false;
	}
	if (elections.size () < config.max_elections)
	{
		return active.vacancy (celerix::election_behavior::priority) < 0;
	}
	return true;
}

bool celerix::scheduler::bucket::activate ()
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };

	if (queue.empty ())
	{
		return false; // Not activated
	}

	block_entry top = *queue.begin ();
	queue.erase (queue.begin ());

	auto block = top.block;
	auto priority = top.time;

	auto erase_callback = [this] (std::shared_ptr<celerix::election> election) {
		celerix::lock_guard<celerix::mutex> lock{ mutex };
		elections.get<tag_root> ().erase (election->qualified_root);
	};

	auto result = active.insert (block, celerix::election_behavior::priority, erase_callback);
	if (result.inserted)
	{
		release_assert (result.election);
		elections.get<tag_root> ().insert ({ result.election, result.election->qualified_root, priority });

		stats.inc (celerix::stat::type::election_bucket, celerix::stat::detail::activate_success);
	}
	else
	{
		stats.inc (celerix::stat::type::election_bucket, celerix::stat::detail::activate_failed);
	}

	return result.inserted;
}

void celerix::scheduler::bucket::update ()
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };

	if (election_overfill ())
	{
		cancel_lowest_election ();
	}
}

// Returns true if the block was inserted
bool celerix::scheduler::bucket::push (uint64_t time, std::shared_ptr<celerix::block> block)
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };

	auto [it, inserted] = queue.insert ({ time, block });
	release_assert (!queue.empty ());
	bool was_last = (it == --queue.end ());
	if (queue.size () > config.max_blocks)
	{
		queue.erase (--queue.end ());
		return inserted && !was_last;
	}
	return inserted;
}

bool celerix::scheduler::bucket::contains (celerix::block_hash const & hash) const
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	return queue.get<tag_hash> ().contains (hash);
}

size_t celerix::scheduler::bucket::size () const
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	return queue.size ();
}

bool celerix::scheduler::bucket::empty () const
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	return queue.empty ();
}

size_t celerix::scheduler::bucket::election_count () const
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	return elections.size ();
}

void celerix::scheduler::bucket::cancel_lowest_election ()
{
	debug_assert (!mutex.try_lock ());

	if (!elections.empty ())
	{
		elections.get<tag_priority> ().begin ()->election->cancel ();

		stats.inc (celerix::stat::type::election_bucket, celerix::stat::detail::cancel_lowest);
	}
}

std::deque<std::shared_ptr<celerix::block>> celerix::scheduler::bucket::blocks () const
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };

	std::deque<std::shared_ptr<celerix::block>> result;
	for (auto const & item : queue)
	{
		result.push_back (item.block);
	}
	return result;
}

void celerix::scheduler::bucket::dump () const
{
	for (auto const & item : queue)
	{
		std::cerr << item.time << ' ' << item.block->hash ().to_string () << '\n';
	}
}

/*
 * priority_bucket_config
 */

celerix::error celerix::scheduler::priority_bucket_config::serialize (celerix::tomlconfig & toml) const
{
	toml.put ("max_blocks", max_blocks, "Maximum number of blocks to sort by priority per bucket. \nType: uint64");
	toml.put ("reserved_elections", reserved_elections, "Number of guaranteed slots per bucket available for election activation. \nType: uint64");
	toml.put ("max_elections", max_elections, "Maximum number of slots per bucket available for election activation if the active election count is below the configured limit. \nType: uint64");

	return toml.get_error ();
}

celerix::error celerix::scheduler::priority_bucket_config::deserialize (celerix::tomlconfig & toml)
{
	toml.get ("max_blocks", max_blocks);
	toml.get ("reserved_elections", reserved_elections);
	toml.get ("max_elections", max_elections);

	return toml.get_error ();
}