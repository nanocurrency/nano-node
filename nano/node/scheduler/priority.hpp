#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/node/fwd.hpp>
#include <nano/node/scheduler/bucket.hpp>

#include <condition_variable>
#include <deque>
#include <memory>
#include <string>
#include <thread>

namespace nano::scheduler
{
class buckets;

class priority_config
{
public:
	// TODO: Serialization & deserialization

public:
	bool enabled{ true };
};

class priority final
{
public:
	priority (nano::node_config &, nano::node &, nano::ledger &, nano::block_processor &, nano::active_elections &, nano::confirming_set &, nano::stats &, nano::logger &);
	~priority ();

	void start ();
	void stop ();

	/**
	 * Activates the first unconfirmed block of \p account_a
	 * @return true if account was activated
	 */
	bool activate (nano::secure::transaction const &, nano::account const &);
	bool activate (nano::secure::transaction const &, nano::account const &, nano::account_info const &, nano::confirmation_height_info const &);
	bool activate_successors (nano::secure::transaction const &, nano::block const &);

	void notify ();
	std::size_t size () const;
	bool empty () const;

	nano::container_info container_info () const;

private: // Dependencies
	priority_config const & config;
	nano::node & node;
	nano::ledger & ledger;
	nano::block_processor & block_processor;
	nano::active_elections & active;
	nano::confirming_set & confirming_set;
	nano::stats & stats;
	nano::logger & logger;

private:
	void run ();
	void run_cleanup ();
	bool predicate () const;
	bucket & find_bucket (nano::uint128_t priority);

private:
	std::vector<std::unique_ptr<bucket>> buckets;

	bool stopped{ false };
	nano::condition_variable condition;
	mutable nano::mutex mutex;
	std::thread thread;
	std::thread cleanup_thread;
};
}
