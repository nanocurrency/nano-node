#include <nano/lib/stats.hpp>
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

nano::vote_processor::vote_processor (nano::active_transactions & active_a, nano::node_observers & observers_a, nano::stats & stats_a, nano::node_config & config_a, nano::node_flags & flags_a, nano::logger & logger_a, nano::online_reps & online_reps_a, nano::rep_crawler & rep_crawler_a, nano::ledger & ledger_a, nano::network_params & network_params_a, nano::rep_tiers & rep_tiers_a) :
	active{ active_a },
	observers{ observers_a },
	stats{ stats_a },
	config{ config_a },
	logger{ logger_a },
	online_reps{ online_reps_a },
	rep_crawler{ rep_crawler_a },
	ledger{ ledger_a },
	network_params{ network_params_a },
	rep_tiers{ rep_tiers_a },
	max_votes{ flags_a.vote_processor_capacity }
{
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

void nano::vote_processor::run ()
{
	nano::timer<std::chrono::milliseconds> elapsed;
	bool log_this_iteration;

	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		if (!votes.empty ())
		{
			decltype (votes) votes_l;
			votes_l.swap (votes);
			lock.unlock ();
			condition.notify_all ();

			log_this_iteration = false;
			// TODO: This is a temporary measure to prevent spamming the logs until we can implement a better solution
			if (votes_l.size () > 1024 * 4)
			{
				/*
				 * Only log the timing information for this iteration if
				 * there are a sufficient number of items for it to be relevant
				 */
				log_this_iteration = true;
				elapsed.restart ();
			}

			for (auto const & [vote, channel] : votes_l)
			{
				vote_blocking (vote, channel);
			}

			total_processed += votes_l.size ();

			if (log_this_iteration && elapsed.stop () > std::chrono::milliseconds (100))
			{
				logger.debug (nano::log::type::vote_processor, "Processed {} votes in {} milliseconds (rate of {} votes per second)",
				votes_l.size (),
				elapsed.value ().count (),
				((votes_l.size () * 1000ULL) / elapsed.value ().count ()));
			}

			lock.lock ();
		}
		else
		{
			condition.wait (lock);
		}
	}
}

bool nano::vote_processor::vote (std::shared_ptr<nano::vote> const & vote_a, std::shared_ptr<nano::transport::channel> const & channel_a)
{
	debug_assert (channel_a != nullptr);

	nano::unique_lock<nano::mutex> lock{ mutex };

	auto should_process = [this] (auto tier) {
		if (votes.size () < 6.0 / 9.0 * max_votes)
		{
			return true;
		}
		// Level 1 (0.1-1%)
		if (votes.size () < 7.0 / 9.0 * max_votes)
		{
			return (tier == nano::rep_tier::tier_1);
		}
		// Level 2 (1-5%)
		if (votes.size () < 8.0 / 9.0 * max_votes)
		{
			return (tier == nano::rep_tier::tier_2);
		}
		// Level 3 (> 5%)
		if (votes.size () < max_votes)
		{
			return (tier == nano::rep_tier::tier_3);
		}
		return false;
	};

	if (!stopped)
	{
		auto tier = rep_tiers.tier (vote_a->account);
		if (should_process (tier))
		{
			votes.emplace_back (vote_a, channel_a);
			lock.unlock ();
			condition.notify_all ();
			// Lock no longer required

			return true; // Processed
		}
		else
		{
			stats.inc (nano::stat::type::vote, nano::stat::detail::vote_overflow);
		}
	}
	return false; // Not processed
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
	return votes.size ();
}

bool nano::vote_processor::empty () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return votes.empty ();
}

std::unique_ptr<nano::container_info_component> nano::vote_processor::collect_container_info (std::string const & name) const
{
	std::size_t votes_count;
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		votes_count = votes.size ();
	}
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "votes", votes_count, sizeof (decltype (votes)::value_type) }));
	return composite;
}
