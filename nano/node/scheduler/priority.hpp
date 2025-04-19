#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/node/fwd.hpp>
#include <nano/node/scheduler/bucket.hpp>

#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <thread>

namespace nano::scheduler
{
class buckets;

class priority_config
{
public:
	nano::error deserialize (nano::tomlconfig &);
	nano::error serialize (nano::tomlconfig &) const;

public:
	bool enable{ true };
};

class priority final
{
public:
	priority (nano::node_config &, nano::node &, nano::ledger &, nano::ledger_notifications &, nano::bucketing &, nano::active_elections &, nano::cementing_set &, nano::stats &, nano::logger &);
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

	bool contains (nano::block_hash const &) const;
	void notify ();
	std::size_t size () const;
	bool empty () const;

	nano::container_info container_info () const;

private: // Dependencies
	priority_config const & config;
	nano::node & node;
	nano::ledger & ledger;
	nano::ledger_notifications & ledger_notifications;
	nano::bucketing & bucketing;
	nano::active_elections & active;
	nano::cementing_set & cementing_set;
	nano::stats & stats;
	nano::logger & logger;

private:
	void run ();
	void run_cleanup ();
	bool predicate () const;

private:
	std::map<nano::bucket_index, std::unique_ptr<scheduler::bucket>> buckets;

	bool stopped{ false };
	nano::condition_variable condition;
	mutable nano::mutex mutex;
	std::thread thread;
	std::thread cleanup_thread;
};
}
