#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/node/election.hpp>
#include <nano/node/prioritization.hpp>

#include <boost/none.hpp>
#include <boost/optional.hpp>

#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <thread>
#include <tuple>

namespace nano
{
class transaction;
class block;
class node;
class election_scheduler final
{
public:
	election_scheduler (nano::node & node);
	~election_scheduler ();
	// Manualy start an election for a block
	// Call action with confirmed block, may be different than what we started with
	void manual (std::shared_ptr<nano::block> const &, boost::optional<nano::uint128_t> const & = boost::none, nano::election_behavior = nano::election_behavior::normal, std::function<void (std::shared_ptr<nano::block> const &)> const & = nullptr);
	// Activates the first unconfirmed block of \p account_a
	void activate (nano::account const &, nano::transaction const &);
	void stop ();
	// Blocks until no more elections can be activated or there are no more elections to activate
	void flush ();
	void notify ();
	size_t size () const;
	bool empty () const;
	size_t priority_queue_size () const;

private:
	void run ();
	bool empty_locked () const;
	bool priority_queue_predicate () const;
	bool manual_queue_predicate () const;
	bool overfill_predicate () const;
	nano::prioritization priority;
	std::deque<std::tuple<std::shared_ptr<nano::block>, boost::optional<nano::uint128_t>, nano::election_behavior, std::function<void (std::shared_ptr<nano::block>)>>> manual_queue;
	nano::node & node;
	bool stopped;
	nano::condition_variable condition;
	mutable nano::mutex mutex;
	std::thread thread;
};
}
