#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/node/active_transactions.hpp>
#include <nano/node/prioritization.hpp>

#include <boost/optional.hpp>

#include <condition_variable>
#include <deque>
#include <memory>
#include <thread>

namespace nano
{
class block;
class node;

class election_scheduler final
{
public:
	election_scheduler (nano::node &, nano::stats &);
	~election_scheduler ();

	void start ();
	void stop ();

	// Manualy start an election for a block
	// Call action with confirmed block, may be different than what we started with
	void manual (std::shared_ptr<nano::block> const &, boost::optional<nano::uint128_t> const & = boost::none, nano::election_behavior = nano::election_behavior::normal);
	/**
	 * Activates the first unconfirmed block of \p account_a
	 * @return true if account was activated
	 */
	bool activate (nano::account const &, nano::transaction const &);
	// Blocks until no more elections can be activated or there are no more elections to activate
	void flush ();
	void notify ();
	std::size_t size () const;
	bool empty () const;
	std::size_t priority_queue_size () const;
	std::unique_ptr<container_info_component> collect_container_info (std::string const &);

private: // Dependencies
	nano::node & node;
	nano::stats & stats;

private:
	void run ();
	bool empty_locked () const;
	bool priority_queue_predicate () const;
	bool manual_queue_predicate () const;

	nano::prioritization priority;

	std::deque<std::tuple<std::shared_ptr<nano::block>, boost::optional<nano::uint128_t>, nano::election_behavior>> manual_queue;
	bool stopped{ false };
	nano::condition_variable condition;
	mutable nano::mutex mutex;
	std::thread thread;
};
}
