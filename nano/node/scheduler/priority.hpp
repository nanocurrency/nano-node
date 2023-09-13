#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/node/active_transactions.hpp>

#include <boost/optional.hpp>

#include <condition_variable>
#include <deque>
#include <memory>
#include <string>
#include <thread>

namespace nano
{
class block;
class container_info_component;
class node;
}

namespace nano::scheduler
{
class buckets;
class priority final
{
	friend class component;
	void start ();
	void stop ();
	std::unique_ptr<container_info_component> collect_container_info (std::string const & name);

public:
	priority (nano::node &, nano::stats &);
	~priority ();

	/**
	 * Activates the first unconfirmed block of \p account_a
	 * @return true if account was activated
	 */
	bool activate (nano::account const &, nano::transaction const &);
	void notify ();
	std::size_t size () const;
	bool empty () const;

private: // Dependencies
	nano::node & node;
	nano::stats & stats;

private:
	void run ();
	bool empty_locked () const;
	bool predicate () const;

	std::unique_ptr<nano::scheduler::buckets> buckets;

	bool stopped{ false };
	nano::condition_variable condition;
	mutable nano::mutex mutex;
	std::thread thread;
};
}
