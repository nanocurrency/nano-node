#include <nano/lib/stats.hpp>
#include <nano/lib/timer.hpp>
#include <nano/node/active_transactions.hpp>
#include <nano/node/node_observers.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/online_reps.hpp>
#include <nano/node/repcrawler.hpp>
#include <nano/node/vote_processor.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>

#include <boost/format.hpp>

#include <chrono>

using namespace std::chrono_literals;

nano::vote_processor::vote_processor (nano::active_transactions & active_a, nano::node_observers & observers_a, nano::stats & stats_a, nano::node_config & config_a, nano::node_flags & flags_a, nano::logger & logger_a, nano::online_reps & online_reps_a, nano::rep_crawler & rep_crawler_a, nano::ledger & ledger_a, nano::network_params & network_params_a) :
	active (active_a),
	observers (observers_a),
	stats (stats_a),
	config (config_a),
	logger (logger_a),
	online_reps (online_reps_a),
	rep_crawler (rep_crawler_a),
	ledger (ledger_a),
	network_params (network_params_a),
	max_votes (flags_a.vote_processor_capacity)
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
			verify_votes (votes_l);
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

auto nano::vote_processor::representative_tier (const nano::account & representative) const -> rep_tier
{
	if (representatives_3.find (representative) != representatives_3.end ())
	{
		return rep_tier::tier_3;
	}
	if (representatives_2.find (representative) != representatives_2.end ())
	{
		return rep_tier::tier_2;
	}
	if (representatives_1.find (representative) != representatives_1.end ())
	{
		return rep_tier::tier_1;
	}
	return rep_tier::tier_none;
}

bool nano::vote_processor::vote (std::shared_ptr<nano::vote> const & vote_a, std::shared_ptr<nano::transport::channel> const & channel_a)
{
	debug_assert (channel_a != nullptr);
	bool process (false);
	nano::unique_lock<nano::mutex> lock{ mutex };
	if (!stopped)
	{
		auto tier = representative_tier (vote_a->account);

		// Level 0 (< 0.1%)
		if (votes.size () < 6.0 / 9.0 * max_votes)
		{
			process = true;
		}
		// Level 1 (0.1-1%)
		else if (votes.size () < 7.0 / 9.0 * max_votes)
		{
			process = (tier == rep_tier::tier_1);
		}
		// Level 2 (1-5%)
		else if (votes.size () < 8.0 / 9.0 * max_votes)
		{
			process = (tier == rep_tier::tier_2);
		}
		// Level 3 (> 5%)
		else if (votes.size () < max_votes)
		{
			process = (tier == rep_tier::tier_3);
		}
		if (process)
		{
			votes.emplace_back (vote_a, channel_a);
			lock.unlock ();
			condition.notify_all ();
			// Lock no longer required
		}
		else
		{
			stats.inc (nano::stat::type::vote, nano::stat::detail::vote_overflow);
		}
	}
	return !process;
}

void nano::vote_processor::verify_votes (decltype (votes) const & votes_a)
{
	for (auto const & vote : votes_a)
	{
		if (!nano::validate_message (vote.first->account, vote.first->hash (), vote.first->signature))
		{
			vote_blocking (vote.first, vote.second, true);
		}
	}
}

nano::vote_code nano::vote_processor::vote_blocking (std::shared_ptr<nano::vote> const & vote_a, std::shared_ptr<nano::transport::channel> const & channel_a, bool validated)
{
	auto result (nano::vote_code::invalid);
	if (validated || !vote_a->validate ())
	{
		result = active.vote (vote_a);
		observers.vote.notify (vote_a, channel_a, result);
	}
	std::string status;
	switch (result)
	{
		case nano::vote_code::invalid:
			status = "Invalid";
			stats.inc (nano::stat::type::vote, nano::stat::detail::vote_invalid);
			break;
		case nano::vote_code::replay:
			status = "Replay";
			stats.inc (nano::stat::type::vote, nano::stat::detail::vote_replay);
			break;
		case nano::vote_code::vote:
			status = "Vote";
			stats.inc (nano::stat::type::vote, nano::stat::detail::vote_valid);
			break;
		case nano::vote_code::indeterminate:
			status = "Indeterminate";
			stats.inc (nano::stat::type::vote, nano::stat::detail::vote_indeterminate);
			break;
	}

	logger.trace (nano::log::type::vote_processor, nano::log::detail::vote_processed,
	nano::log::arg{ "vote", vote_a },
	nano::log::arg{ "result", result });

	return result;
}

void nano::vote_processor::flush ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	auto const cutoff = total_processed.load (std::memory_order_relaxed) + votes.size ();
	bool success = condition.wait_for (lock, 60s, [this, &cutoff] () {
		return stopped || votes.empty () || total_processed.load (std::memory_order_relaxed) >= cutoff;
	});
	if (!success)
	{
		logger.error (nano::log::type::vote_processor, "Flush timeout");
		debug_assert (false && "vote_processor::flush timeout while waiting for flush");
	}
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

bool nano::vote_processor::half_full () const
{
	return size () >= max_votes / 2;
}

void nano::vote_processor::calculate_weights ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	if (!stopped)
	{
		representatives_1.clear ();
		representatives_2.clear ();
		representatives_3.clear ();
		auto supply (online_reps.trended ());
		auto rep_amounts = ledger.cache.rep_weights.get_rep_amounts ();
		for (auto const & rep_amount : rep_amounts)
		{
			// TODO: Base this calculation on online weight, not total supply
			nano::account const & representative (rep_amount.first);
			auto weight (ledger.weight (representative));
			if (weight > supply / 1000) // 0.1% or above (level 1)
			{
				representatives_1.insert (representative);
				if (weight > supply / 100) // 1% or above (level 2)
				{
					representatives_2.insert (representative);
					if (weight > supply / 20) // 5% or above (level 3)
					{
						representatives_3.insert (representative);
					}
				}
			}
		}
	}
}

std::unique_ptr<nano::container_info_component> nano::collect_container_info (vote_processor & vote_processor, std::string const & name)
{
	std::size_t votes_count;
	std::size_t representatives_1_count;
	std::size_t representatives_2_count;
	std::size_t representatives_3_count;

	{
		nano::lock_guard<nano::mutex> guard{ vote_processor.mutex };
		votes_count = vote_processor.votes.size ();
		representatives_1_count = vote_processor.representatives_1.size ();
		representatives_2_count = vote_processor.representatives_2.size ();
		representatives_3_count = vote_processor.representatives_3.size ();
	}

	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "votes", votes_count, sizeof (decltype (vote_processor.votes)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "representatives_1", representatives_1_count, sizeof (decltype (vote_processor.representatives_1)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "representatives_2", representatives_2_count, sizeof (decltype (vote_processor.representatives_2)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "representatives_3", representatives_3_count, sizeof (decltype (vote_processor.representatives_3)::value_type) }));
	return composite;
}
