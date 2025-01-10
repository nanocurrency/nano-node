#pragma once

#include <celerix/lib/numbers.hpp>
#include <celerix/lib/threading.hpp>
#include <celerix/lib/utility.hpp>
#include <celerix/node/fair_queue.hpp>
#include <celerix/node/fwd.hpp>
#include <celerix/node/rep_tiers.hpp>
#include <celerix/node/vote_router.hpp>
#include <celerix/secure/common.hpp>

#include <deque>
#include <memory>
#include <thread>
#include <unordered_set>

namespace celerix
{
class vote_processor_config final
{
public:
	celerix::error serialize (celerix::tomlconfig & toml) const;
	celerix::error deserialize (celerix::tomlconfig & toml);

public:
	bool enable{ true };

	size_t max_pr_queue{ 256 };
	size_t max_non_pr_queue{ 32 };
	size_t pr_priority{ 3 };
	size_t threads{ std::clamp (celerix::hardware_concurrency () / 2, 1u, 4u) };
	size_t batch_size{ 1024 };
	size_t max_triggered{ 16384 };
};

class vote_processor final
{
public:
	vote_processor (vote_processor_config const &, celerix::vote_router &, celerix::node_observers &, celerix::stats &, celerix::node_flags &, celerix::logger &, celerix::online_reps &, celerix::rep_crawler &, celerix::ledger &, celerix::network_params &, celerix::rep_tiers &);
	~vote_processor ();

	void start ();
	void stop ();

	/** Queue vote for processing. @returns true if the vote was queued */
	bool vote (std::shared_ptr<celerix::vote> const &, std::shared_ptr<celerix::transport::channel> const &, celerix::vote_source = celerix::vote_source::live);
	celerix::vote_code vote_blocking (std::shared_ptr<celerix::vote> const &, std::shared_ptr<celerix::transport::channel> const &, celerix::vote_source = celerix::vote_source::live);

	/** Queue hash for vote cache lookup and processing. */
	void trigger (celerix::block_hash const & hash);

	std::size_t size () const;
	bool empty () const;

	celerix::container_info container_info () const;

	std::atomic<uint64_t> total_processed{ 0 };

private: // Dependencies
	vote_processor_config const & config;
	celerix::vote_router & vote_router;
	celerix::node_observers & observers;
	celerix::stats & stats;
	celerix::logger & logger;
	celerix::online_reps & online_reps;
	celerix::rep_crawler & rep_crawler;
	celerix::ledger & ledger;
	celerix::network_params & network_params;
	celerix::rep_tiers & rep_tiers;

private:
	void run ();
	void run_batch (celerix::unique_lock<celerix::mutex> &);

private:
	using entry_t = std::pair<std::shared_ptr<celerix::vote>, celerix::vote_source>;
	celerix::fair_queue<entry_t, celerix::rep_tier> queue;

private:
	bool stopped{ false };
	celerix::condition_variable condition;
	mutable celerix::mutex mutex{ mutex_identifier (mutexes::vote_processor) };
	std::vector<std::thread> threads;
};

class vote_cache_processor final
{
public:
	vote_cache_processor (vote_processor_config const &, celerix::vote_router &, celerix::vote_cache &, celerix::stats &, celerix::logger &);
	~vote_cache_processor ();

	void start ();
	void stop ();

	/** Queue hash for vote cache lookup and processing. */
	void trigger (celerix::block_hash const & hash);

	std::size_t size () const;
	bool empty () const;

	celerix::container_info container_info () const;

private:
	void run ();
	void run_batch (celerix::unique_lock<celerix::mutex> &);

private: // Dependencies
	vote_processor_config const & config;
	celerix::vote_router & vote_router;
	celerix::vote_cache & vote_cache;
	celerix::stats & stats;
	celerix::logger & logger;

private:
	std::deque<celerix::block_hash> triggered;

	bool stopped{ false };
	celerix::condition_variable condition;
	mutable celerix::mutex mutex;
	std::thread thread;
};
}
