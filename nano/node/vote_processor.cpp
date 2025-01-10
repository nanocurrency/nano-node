#include <celerix/lib/stats.hpp>
#include <celerix/lib/thread_roles.hpp>
#include <celerix/lib/timer.hpp>
#include <celerix/node/node_observers.hpp>
#include <celerix/node/nodeconfig.hpp>
#include <celerix/node/online_reps.hpp>
#include <celerix/node/rep_tiers.hpp>
#include <celerix/node/repcrawler.hpp>
#include <celerix/node/vote_processor.hpp>
#include <celerix/node/vote_router.hpp>
#include <celerix/secure/common.hpp>
#include <celerix/secure/ledger.hpp>
#include <celerix/secure/vote.hpp>

#include <chrono>

using namespace std::chrono_literals;

/*
 * vote_processor
 */

celerix::vote_processor::vote_processor (vote_processor_config const & config_a, celerix::vote_router & vote_router, celerix::node_observers & observers_a, celerix::stats & stats_a, celerix::node_flags & flags_a, celerix::logger & logger_a, celerix::online_reps & online_reps_a, celerix::rep_crawler & rep_crawler_a, celerix::ledger & ledger_a, celerix::network_params & network_params_a, celerix::rep_tiers & rep_tiers_a) :
	config{ config_a },
	vote_router{ vote_router },
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
			case celerix::rep_tier::tier_3:
			case celerix::rep_tier::tier_2:
			case celerix::rep_tier::tier_1:
				return config.max_pr_queue;
			case celerix::rep_tier::none:
				return config.max_non_pr_queue;
		}
		debug_assert (false);
		return size_t{ 0 };
	};

	queue.priority_query = [this] (auto const & origin) {
		switch (origin.source)
		{
			case celerix::rep_tier::tier_3:
				return config.pr_priority * config.pr_priority * config.pr_priority;
			case celerix::rep_tier::tier_2:
				return config.pr_priority * config.pr_priority;
			case celerix::rep_tier::tier_1:
				return config.pr_priority;
			case celerix::rep_tier::none:
				return size_t{ 1 };
		}
		debug_assert (false);
		return size_t{ 0 };
	};
}

celerix::vote_processor::~vote_processor ()
{
	debug_assert (threads.empty ());
}

void celerix::vote_processor::start ()
{
	debug_assert (threads.empty ());

	if (!config.enable)
	{
		return;
	}

	for (int n = 0; n < config.threads; ++n)
	{
		threads.emplace_back ([this] () {
			celerix::thread_role::set (celerix::thread_role::name::vote_processing);
			run ();
		});
	}
}

void celerix::vote_processor::stop ()
{
	{
		celerix::lock_guard<celerix::mutex> lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();

	for (auto & thread : threads)
	{
		thread.join ();
	}
	threads.clear ();
}

bool celerix::vote_processor::vote (std::shared_ptr<celerix::vote> const & vote, std::shared_ptr<celerix::transport::channel> const & channel, celerix::vote_source source)
{
	debug_assert (channel != nullptr);

	auto const tier = rep_tiers.tier (vote->account);

	bool added = false;
	{
		celerix::lock_guard<celerix::mutex> guard{ mutex };
		added = queue.push ({ vote, source }, { tier, channel });
	}
	if (added)
	{
		stats.inc (celerix::stat::type::vote_processor, celerix::stat::detail::process);
		stats.inc (celerix::stat::type::vote_processor_tier, to_stat_detail (tier));

		condition.notify_one ();
	}
	else
	{
		stats.inc (celerix::stat::type::vote_processor, celerix::stat::detail::overfill);
		stats.inc (celerix::stat::type::vote_processor_overfill, to_stat_detail (tier));
	}
	return added;
}

void celerix::vote_processor::run ()
{
	celerix::unique_lock<celerix::mutex> lock{ mutex };
	while (!stopped)
	{
		stats.inc (celerix::stat::type::vote_processor, celerix::stat::detail::loop);

		if (!queue.empty ())
		{
			run_batch (lock);
			debug_assert (!lock.owns_lock ());
			lock.lock ();
		}
		else
		{
			condition.wait (lock, [&] { return stopped || !queue.empty (); });
		}
	}
}

void celerix::vote_processor::run_batch (celerix::unique_lock<celerix::mutex> & lock)
{
	debug_assert (lock.owns_lock ());
	debug_assert (!mutex.try_lock ());
	debug_assert (!queue.empty ());

	celerix::timer<std::chrono::milliseconds> timer;
	timer.start ();

	auto batch = queue.next_batch (config.batch_size);

	lock.unlock ();

	for (auto const & [item, origin] : batch)
	{
		auto const & [vote, source] = item;
		vote_blocking (vote, origin.channel, source);
	}

	total_processed += batch.size ();

	if (batch.size () == config.batch_size && timer.stop () > 100ms)
	{
		logger.debug (celerix::log::type::vote_processor, "Processed {} votes in {} milliseconds (rate of {} votes per second)",
		batch.size (),
		timer.value ().count (),
		((batch.size () * 1000ULL) / timer.value ().count ()));
	}
}

celerix::vote_code celerix::vote_processor::vote_blocking (std::shared_ptr<celerix::vote> const & vote, std::shared_ptr<celerix::transport::channel> const & channel, celerix::vote_source source)
{
	auto result = celerix::vote_code::invalid;
	if (!vote->validate ()) // false => valid vote
	{
		auto vote_results = vote_router.vote (vote, source);

		// Aggregate results for individual hashes
		bool replay = false;
		bool processed = false;
		for (auto const & [hash, hash_result] : vote_results)
		{
			replay |= (hash_result == celerix::vote_code::replay);
			processed |= (hash_result == celerix::vote_code::vote);
		}
		result = replay ? celerix::vote_code::replay : (processed ? celerix::vote_code::vote : celerix::vote_code::indeterminate);

		observers.vote.notify (vote, channel, source, result);
	}

	stats.inc (celerix::stat::type::vote, to_stat_detail (result));

	logger.trace (celerix::log::type::vote_processor, celerix::log::detail::vote_processed,
	celerix::log::arg{ "vote", vote },
	celerix::log::arg{ "vote_source", source },
	celerix::log::arg{ "result", result });

	return result;
}

std::size_t celerix::vote_processor::size () const
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };
	return queue.size ();
}

bool celerix::vote_processor::empty () const
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };
	return queue.empty ();
}

celerix::container_info celerix::vote_processor::container_info () const
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };

	celerix::container_info info;
	info.put ("votes", queue.size ());
	info.add ("queue", queue.container_info ());
	return info;
}

/*
 * vote_cache_processor
 */

celerix::vote_cache_processor::vote_cache_processor (vote_processor_config const & config_a, celerix::vote_router & vote_router_a, celerix::vote_cache & vote_cache_a, celerix::stats & stats_a, celerix::logger & logger_a) :
	config{ config_a },
	vote_router{ vote_router_a },
	vote_cache{ vote_cache_a },
	stats{ stats_a },
	logger{ logger_a }
{
}

celerix::vote_cache_processor::~vote_cache_processor ()
{
	debug_assert (!thread.joinable ());
}

void celerix::vote_cache_processor::start ()
{
	debug_assert (!thread.joinable ());

	thread = std::thread ([this] () {
		celerix::thread_role::set (celerix::thread_role::name::vote_cache_processing);
		run ();
	});
}

void celerix::vote_cache_processor::stop ()
{
	{
		celerix::lock_guard<celerix::mutex> guard{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	if (thread.joinable ())
	{
		thread.join ();
	}
}

void celerix::vote_cache_processor::trigger (celerix::block_hash const & hash)
{
	{
		celerix::lock_guard<celerix::mutex> guard{ mutex };
		if (triggered.size () >= config.max_triggered)
		{
			triggered.pop_front ();
			stats.inc (celerix::stat::type::vote_cache_processor, celerix::stat::detail::overfill);
		}
		triggered.push_back (hash);
	}
	condition.notify_all ();
	stats.inc (celerix::stat::type::vote_cache_processor, celerix::stat::detail::triggered);
}

void celerix::vote_cache_processor::run ()
{
	celerix::unique_lock<celerix::mutex> lock{ mutex };
	while (!stopped)
	{
		stats.inc (celerix::stat::type::vote_cache_processor, celerix::stat::detail::loop);

		if (!triggered.empty ())
		{
			run_batch (lock);
			debug_assert (!lock.owns_lock ());
			lock.lock ();
		}
		else
		{
			condition.wait (lock, [&] { return stopped || !triggered.empty (); });
		}
	}
}

void celerix::vote_cache_processor::run_batch (celerix::unique_lock<celerix::mutex> & lock)
{
	debug_assert (lock.owns_lock ());
	debug_assert (!mutex.try_lock ());
	debug_assert (!triggered.empty ());

	// Swap and deduplicate
	decltype (triggered) triggered_l;
	swap (triggered_l, triggered);

	lock.unlock ();

	std::unordered_set<celerix::block_hash> hashes;
	hashes.reserve (triggered_l.size ());
	hashes.insert (triggered_l.begin (), triggered_l.end ());

	stats.add (celerix::stat::type::vote_cache_processor, celerix::stat::detail::processed, hashes.size ());

	for (auto const & hash : hashes)
	{
		auto cached = vote_cache.find (hash);
		for (auto const & cached_vote : cached)
		{
			vote_router.vote (cached_vote, celerix::vote_source::cache, hash);
		}
	}
}

std::size_t celerix::vote_cache_processor::size () const
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };
	return triggered.size ();
}

bool celerix::vote_cache_processor::empty () const
{
	return size () == 0;
}

celerix::container_info celerix::vote_cache_processor::container_info () const
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };

	celerix::container_info info;
	info.put ("triggered", triggered.size ());
	return info;
}

/*
 * vote_processor_config
 */

celerix::error celerix::vote_processor_config::serialize (celerix::tomlconfig & toml) const
{
	toml.put ("max_pr_queue", max_pr_queue, "Maximum number of votes to queue from principal representatives. \ntype:uint64");
	toml.put ("max_non_pr_queue", max_non_pr_queue, "Maximum number of votes to queue from non-principal representatives. \ntype:uint64");
	toml.put ("pr_priority", pr_priority, "Priority for votes from principal representatives. Higher priority gets processed more frequently. Non-principal representatives have a baseline priority of 1. \ntype:uint64");
	toml.put ("threads", threads, "Number of threads to use for processing votes. \ntype:uint64");
	toml.put ("batch_size", batch_size, "Maximum number of votes to process in a single batch. \ntype:uint64");

	return toml.get_error ();
}

celerix::error celerix::vote_processor_config::deserialize (celerix::tomlconfig & toml)
{
	toml.get ("max_pr_queue", max_pr_queue);
	toml.get ("max_non_pr_queue", max_non_pr_queue);
	toml.get ("pr_priority", pr_priority);
	toml.get ("threads", threads);
	toml.get ("batch_size", batch_size);

	return toml.get_error ();
}
