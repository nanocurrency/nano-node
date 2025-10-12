#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/node/fwd.hpp>

#include <condition_variable>
#include <deque>
#include <thread>

namespace nano
{
class pruning final
{
public:
	pruning (nano::node_config const &, nano::node_flags const &, nano::ledger &, nano::stats &, nano::logger &);
	~pruning ();

	void start ();
	void stop ();

	nano::container_info container_info () const;

	void ledger_pruning (uint64_t batch_size, bool bootstrap_weight_reached);
	bool collect_ledger_pruning_targets (std::deque<nano::block_hash> & pruning_targets_out, nano::account & last_account_out, uint64_t batch_read_size, uint64_t max_depth, uint64_t cutoff_time);

private: // Dependencies
	nano::node_config const & config;
	nano::node_flags const & flags;
	nano::ledger & ledger;
	nano::stats & stats;
	nano::logger & logger;

private:
	void run ();

private:
	bool stopped{ false };
	nano::condition_variable condition;
	mutable nano::mutex mutex;
	std::thread thread;
};
}