#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/threading.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/fair_queue.hpp>
#include <nano/node/fwd.hpp>
#include <nano/node/rep_tiers.hpp>
#include <nano/node/vote_router.hpp>
#include <nano/secure/common.hpp>

#include <deque>
#include <memory>
#include <thread>
#include <unordered_set>

namespace nano
{
class vote_processor_config final
{
public:
	nano::error serialize (nano::tomlconfig & toml) const;
	nano::error deserialize (nano::tomlconfig & toml);

public:
	size_t max_pr_queue{ 256 };
	size_t max_non_pr_queue{ 32 };
	size_t pr_priority{ 3 };
	size_t threads{ std::clamp (nano::hardware_concurrency () / 2, 1u, 4u) };
	size_t batch_size{ 1024 };
	size_t max_triggered{ 16384 };
};

class vote_processor final
{
public:
	vote_processor (vote_processor_config const &, nano::vote_router &, nano::node_observers &, nano::stats &, nano::node_flags &, nano::logger &, nano::online_reps &, nano::rep_crawler &, nano::ledger &, nano::network_params &, nano::rep_tiers &);
	~vote_processor ();

	void start ();
	void stop ();

	/** Queue vote for processing. @returns true if the vote was queued */
	bool vote (std::shared_ptr<nano::vote> const &, std::shared_ptr<nano::transport::channel> const &, nano::vote_source = nano::vote_source::live);
	nano::vote_code vote_blocking (std::shared_ptr<nano::vote> const &, std::shared_ptr<nano::transport::channel> const &, nano::vote_source = nano::vote_source::live);

	/** Queue hash for vote cache lookup and processing. */
	void trigger (nano::block_hash const & hash);

	std::size_t size () const;
	bool empty () const;

	std::unique_ptr<container_info_component> collect_container_info (std::string const & name) const;

	std::atomic<uint64_t> total_processed{ 0 };

private: // Dependencies
	vote_processor_config const & config;
	nano::vote_router & vote_router;
	nano::node_observers & observers;
	nano::stats & stats;
	nano::logger & logger;
	nano::online_reps & online_reps;
	nano::rep_crawler & rep_crawler;
	nano::ledger & ledger;
	nano::network_params & network_params;
	nano::rep_tiers & rep_tiers;

private:
	void run ();
	void run_batch (nano::unique_lock<nano::mutex> &);

private:
	using entry_t = std::pair<std::shared_ptr<nano::vote>, nano::vote_source>;
	nano::fair_queue<entry_t, nano::rep_tier> queue;

private:
	bool stopped{ false };
	nano::condition_variable condition;
	mutable nano::mutex mutex{ mutex_identifier (mutexes::vote_processor) };
	std::vector<std::thread> threads;
};

class vote_cache_processor final
{
public:
	vote_cache_processor (vote_processor_config const &, nano::vote_router &, nano::vote_cache &, nano::stats &, nano::logger &);
	~vote_cache_processor ();

	void start ();
	void stop ();

	/** Queue hash for vote cache lookup and processing. */
	void trigger (nano::block_hash const & hash);

	std::size_t size () const;
	bool empty () const;

	std::unique_ptr<container_info_component> collect_container_info (std::string const & name) const;

private:
	void run ();
	void run_batch (nano::unique_lock<nano::mutex> &);

private: // Dependencies
	vote_processor_config const & config;
	nano::vote_router & vote_router;
	nano::vote_cache & vote_cache;
	nano::stats & stats;
	nano::logger & logger;

private:
	std::deque<nano::block_hash> triggered;

	bool stopped{ false };
	nano::condition_variable condition;
	mutable nano::mutex mutex;
	std::thread thread;
};
}
