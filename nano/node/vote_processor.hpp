#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/fair_queue.hpp>
#include <nano/node/rep_tiers.hpp>
#include <nano/secure/common.hpp>

#include <deque>
#include <memory>
#include <thread>
#include <unordered_set>

namespace nano
{
class active_transactions;
namespace store
{
	class component;
}
class node_observers;
class stats;
class node_config;
class logger;
class online_reps;
class rep_crawler;
class ledger;
class network_params;
class node_flags;
class stats;
class rep_tiers;

namespace transport
{
	class channel;
}
}

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
};

class vote_processor final
{
public:
	vote_processor (vote_processor_config const &, nano::active_transactions &, nano::node_observers &, nano::stats &, nano::node_flags &, nano::logger &, nano::online_reps &, nano::rep_crawler &, nano::ledger &, nano::network_params &, nano::rep_tiers &);
	~vote_processor ();

	void start ();
	void stop ();

	/** @returns true if the vote was queued for processing */
	bool vote (std::shared_ptr<nano::vote> const &, std::shared_ptr<nano::transport::channel> const &);
	nano::vote_code vote_blocking (std::shared_ptr<nano::vote> const &, std::shared_ptr<nano::transport::channel> const &);

	std::size_t size () const;
	bool empty () const;

	std::unique_ptr<container_info_component> collect_container_info (std::string const & name) const;

	std::atomic<uint64_t> total_processed{ 0 };

private: // Dependencies
	vote_processor_config const & config;
	nano::active_transactions & active;
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
	nano::fair_queue<std::shared_ptr<nano::vote>, nano::rep_tier> queue;

private:
	bool stopped{ false };
	nano::condition_variable condition;
	mutable nano::mutex mutex{ mutex_identifier (mutexes::vote_processor) };
	std::thread thread;
};
}
