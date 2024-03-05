#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/utility.hpp>
#include <nano/secure/common.hpp>

#include <deque>
#include <memory>
#include <thread>
#include <unordered_set>

namespace nano
{
class signature_checker;
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

namespace transport
{
	class channel;
}

class vote_processor final
{
public:
	vote_processor (nano::active_transactions & active_a, nano::node_observers & observers_a, nano::stats & stats_a, nano::node_config & config_a, nano::node_flags & flags_a, nano::logger &, nano::online_reps & online_reps_a, nano::rep_crawler & rep_crawler_a, nano::ledger & ledger_a, nano::network_params & network_params_a);
	~vote_processor ();

	void start ();
	void stop ();

	/** Returns false if the vote was processed */
	bool vote (std::shared_ptr<nano::vote> const &, std::shared_ptr<nano::transport::channel> const &);
	/** Note: node.active.mutex lock is required */
	nano::vote_code vote_blocking (std::shared_ptr<nano::vote> const &, std::shared_ptr<nano::transport::channel> const &, bool = false);
	void verify_votes (std::deque<std::pair<std::shared_ptr<nano::vote>, std::shared_ptr<nano::transport::channel>>> const &);
	/** Function blocks until either the current queue size (a established flush boundary as it'll continue to increase)
	 * is processed or the queue is empty (end condition or cutoff's guard, as it is positioned ahead) */
	void flush ();
	std::size_t size () const;
	bool empty () const;
	bool half_full () const;
	void calculate_weights ();

	std::atomic<uint64_t> total_processed{ 0 };

private: // Dependencies
	nano::active_transactions & active;
	nano::node_observers & observers;
	nano::stats & stats;
	nano::node_config & config;
	nano::logger & logger;
	nano::online_reps & online_reps;
	nano::rep_crawler & rep_crawler;
	nano::ledger & ledger;
	nano::network_params & network_params;

private:
	void run ();

	std::size_t const max_votes;
	std::deque<std::pair<std::shared_ptr<nano::vote>, std::shared_ptr<nano::transport::channel>>> votes;

	/** Representatives levels for random early detection */
	std::unordered_set<nano::account> representatives_1;
	std::unordered_set<nano::account> representatives_2;
	std::unordered_set<nano::account> representatives_3;

private:
	bool stopped{ false };
	nano::condition_variable condition;
	mutable nano::mutex mutex{ mutex_identifier (mutexes::vote_processor) };
	std::thread thread;

	friend std::unique_ptr<container_info_component> collect_container_info (vote_processor & vote_processor, std::string const & name);
	friend class vote_processor_weights_Test;
};

std::unique_ptr<container_info_component> collect_container_info (vote_processor & vote_processor, std::string const & name);
}
