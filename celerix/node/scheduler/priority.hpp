#pragma once

#include <celerix/lib/numbers.hpp>
#include <celerix/node/fwd.hpp>
#include <celerix/node/scheduler/bucket.hpp>

#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <thread>

namespace celerix::scheduler
{
class buckets;

class priority_config
{
public:
	// TODO: Serialization & deserialization

public:
	bool enable{ true };
};

class priority final
{
public:
	priority (celerix::node_config &, celerix::node &, celerix::ledger &, celerix::bucketing &, celerix::block_processor &, celerix::active_elections &, celerix::confirming_set &, celerix::stats &, celerix::logger &);
	~priority ();

	void start ();
	void stop ();

	/**
	 * Activates the first unconfirmed block of \p account_a
	 * @return true if account was activated
	 */
	bool activate (celerix::secure::transaction const &, celerix::account const &);
	bool activate (celerix::secure::transaction const &, celerix::account const &, celerix::account_info const &, celerix::confirmation_height_info const &);
	bool activate_successors (celerix::secure::transaction const &, celerix::block const &);

	bool contains (celerix::block_hash const &) const;
	void notify ();
	std::size_t size () const;
	bool empty () const;

	celerix::container_info container_info () const;

private: // Dependencies
	priority_config const & config;
	celerix::node & node;
	celerix::ledger & ledger;
	celerix::bucketing & bucketing;
	celerix::block_processor & block_processor;
	celerix::active_elections & active;
	celerix::confirming_set & confirming_set;
	celerix::stats & stats;
	celerix::logger & logger;

private:
	void run ();
	void run_cleanup ();
	bool predicate () const;

private:
	std::map<celerix::bucket_index, std::unique_ptr<scheduler::bucket>> buckets;

	bool stopped{ false };
	celerix::condition_variable condition;
	mutable celerix::mutex mutex;
	std::thread thread;
	std::thread cleanup_thread;
};
}
