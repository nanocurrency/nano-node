#include <nano/lib/stats.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/lib/timer.hpp>
#include <nano/node/active_transactions.hpp>
#include <nano/node/node_observers.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/online_reps.hpp>
#include <nano/node/rep_tiers.hpp>
#include <nano/node/repcrawler.hpp>
#include <nano/node/vote_processor.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>

#include <chrono>

using namespace std::chrono_literals;

nano::vote_processor::vote_processor (vote_processor_config const & config_a, nano::active_transactions & active_a, nano::node_observers & observers_a, nano::stats & stats_a, nano::node_flags & flags_a, nano::logger & logger_a, nano::online_reps & online_reps_a, nano::rep_crawler & rep_crawler_a, nano::ledger & ledger_a, nano::network_params & network_params_a, nano::rep_tiers & rep_tiers_a) :
	config{ config_a },
	active{ active_a },
	observers{ observers_a },
	stats{ stats_a },
	logger{ logger_a },
	online_reps{ online_reps_a },
	rep_crawler{ rep_crawler_a },
	ledger{ ledger_a },
	network_params{ network_params_a },
	rep_tiers{ rep_tiers_a }
{
	queue.max_size_query = [this] (auto const & origin) {
		switch (origin.source)
		{
			case nano::rep_tier::tier_3:
			case nano::rep_tier::tier_2:
			case nano::rep_tier::tier_1:
				return config.max_pr_queue;
			case nano::rep_tier::none:
				return config.max_non_pr_queue;
		}
		debug_assert (false);
		return size_t{ 0 };
	};

	queue.priority_query = [this] (auto const & origin) {
		switch (origin.source)
		{
			case nano::rep_tier::tier_3:
				return config.pr_priority * config.pr_priority * config.pr_priority;
			case nano::rep_tier::tier_2:
				return config.pr_priority * config.pr_priority;
			case nano::rep_tier::tier_1:
				return config.pr_priority;
			case nano::rep_tier::none:
				return size_t{ 1 };
		}
		debug_assert (false);
		return size_t{ 0 };
	};
}

nano::vote_processor::~vote_processor ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void nano::vote_processor::start ()
{
	debug_assert (!thread.joinable ());

	thread = std::thread{ [this] () {
		nano::thread_role::set (nano::thread_role::name::vote_processing);
		run ();
	} };
}

void nano::vote_processor::stop ()
{
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	if (thread.joinable ())
	{
		thread.join ();
	}
}

bool nano::vote_processor::vote (std::shared_ptr<nano::vote> const & vote, std::shared_ptr<nano::transport::channel> const & channel)
{
	debug_assert (channel != nullptr);

	auto const tier = rep_tiers.tier (vote->account);

	bool added = false;
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		added = queue.push (vote, { tier, channel });
	}
	if (added)
	{
		stats.inc (nano::stat::type::vote_processor, nano::stat::detail::process);
		stats.inc (nano::stat::type::vote_processor_tier, to_stat_detail (tier));

		condition.notify_all ();
	}
	else
	{
		stats.inc (nano::stat::type::vote_processor, nano::stat::detail::overfill);
		stats.inc (nano::stat::type::vote_processor_overfill, to_stat_detail (tier));
	}
	return added;
}

void nano::vote_processor::run ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		stats.inc (nano::stat::type::vote_processor, nano::stat::detail::loop);

		if (!queue.empty ())
		{
			run_batch (lock);
			debug_assert (!lock.owns_lock ());

			lock.lock ();
		}

		condition.wait (lock, [&] {
			return stopped || !queue.empty ();
		});
	}
}

void nano::vote_processor::run_batch (nano::unique_lock<nano::mutex> & lock)
{
	debug_assert (lock.owns_lock ());
	debug_assert (!mutex.try_lock ());
	debug_assert (!queue.empty ());

	nano::timer<std::chrono::milliseconds> timer;

	size_t const max_batch_size = 1024 * 4;
	auto batch = queue.next_batch (max_batch_size);

	lock.unlock ();

	for (auto const & [vote, origin] : batch)
	{
		vote_blocking (vote, origin.channel);
	}

	total_processed += batch.size ();

	if (batch.size () == max_batch_size && timer.stop () > 100ms)
	{
		logger.debug (nano::log::type::vote_processor, "Processed {} votes in {} milliseconds (rate of {} votes per second)",
		batch.size (),
		timer.value ().count (),
		((batch.size () * 1000ULL) / timer.value ().count ()));
	}
}

nano::vote_code nano::vote_processor::vote_blocking (std::shared_ptr<nano::vote> const & vote, std::shared_ptr<nano::transport::channel> const & channel)
{
	auto result = nano::vote_code::invalid;
	if (!vote->validate ()) // false => valid vote
	{
		auto vote_results = active.vote (vote);

		// Aggregate results for individual hashes
		bool replay = false;
		bool processed = false;
		for (auto const & [hash, hash_result] : vote_results)
		{
			replay |= (hash_result == nano::vote_code::replay);
			processed |= (hash_result == nano::vote_code::vote);
		}
		result = replay ? nano::vote_code::replay : (processed ? nano::vote_code::vote : nano::vote_code::indeterminate);

		observers.vote.notify (vote, channel, result);
	}

	stats.inc (nano::stat::type::vote, to_stat_detail (result));

	logger.trace (nano::log::type::vote_processor, nano::log::detail::vote_processed,
	nano::log::arg{ "vote", vote },
	nano::log::arg{ "result", result });

	return result;
}

std::size_t nano::vote_processor::size () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return queue.size ();
}

bool nano::vote_processor::empty () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return queue.empty ();
}

std::unique_ptr<nano::container_info_component> nano::vote_processor::collect_container_info (std::string const & name) const
{
	std::size_t votes_count;
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		votes_count = queue.size ();
	}
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "votes", votes_count, sizeof (decltype (queue)::value_type) }));
	return composite;
}

/*
 * vote_processor_config
 */

nano::error nano::vote_processor_config::serialize (nano::tomlconfig & toml) const
{
	toml.put ("max_pr_queue", max_pr_queue, "Maximum number of votes to queue from principal representatives. \ntype:uint64");
	toml.put ("max_non_pr_queue", max_non_pr_queue, "Maximum number of votes to queue from non-principal representatives. \ntype:uint64");
	toml.put ("pr_priority", pr_priority, "Priority for votes from principal representatives. Higher priority gets processed more frequently. Non-principal representatives have a baseline priority of 1. \ntype:uint64");

	return toml.get_error ();
}

nano::error nano::vote_processor_config::deserialize (nano::tomlconfig & toml)
{
	toml.get ("max_pr_queue", max_pr_queue);
	toml.get ("max_non_pr_queue", max_non_pr_queue);
	toml.get ("pr_priority", pr_priority);

	return toml.get_error ();
}
