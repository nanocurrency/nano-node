#pragma once

#include <nano/lib/numbers.hpp>

#include <boost/optional.hpp>

#include <condition_variable>
#include <deque>
#include <memory>
#include <string>
#include <thread>

namespace nano
{
class active_elections;
class block;
class container_info_component;
class ledger;
class stats;
}
namespace nano::secure
{
class transaction;
}

namespace nano::scheduler
{
class priority_config
{
public:
	// TODO: Serialization & deserialization

public:
	bool enabled{ true };
	size_t bucket_maximum{ 128 };
};

class buckets;
class priority final
{
public:
	priority (priority_config const & config, nano::ledger & ledger, nano::active_elections & active, nano::stats & stats, nano::logger & logger);
	~priority ();

	void start ();
	void stop ();

	/**
	 * Activates the first unconfirmed block of \p account_a
	 * @return true if account was activated
	 */
	bool activate (secure::transaction const &, nano::account const &);
	void notify ();
	std::size_t size () const;
	bool empty () const;

	std::unique_ptr<container_info_component> collect_container_info (std::string const & name);

private: // Dependencies
	priority_config const & config;
	nano::ledger & ledger;
	nano::active_elections & active;
	nano::stats & stats;
	nano::logger & logger;

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
