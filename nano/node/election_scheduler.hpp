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
	election_scheduler (nano::node & node);
	~election_scheduler ();
	// Manualy start an election for a block
	// Call action with confirmed block, may be different than what we started with
	void manual (std::shared_ptr<nano::block> const &, boost::optional<nano::uint128_t> const & = boost::none, nano::election_behavior = nano::election_behavior::normal, std::function<void(std::shared_ptr<nano::block> const&)> const & = nullptr);
	// Activates the first unconfirmed block of \p account_a
	void activate (nano::account const &, nano::transaction const &);
	void stop ();
	void flush ();
private:
	void run ();
	nano::prioritization priority;
	uint64_t priority_queued{ 0 };
	std::deque<std::tuple<std::shared_ptr<nano::block>, boost::optional<nano::uint128_t>, nano::election_behavior, std::function<void(std::shared_ptr<nano::block>)>>> manual_queue;
	uint64_t manual_queued{ 0 };
	nano::node & node;
	bool stopped;
	std::condition_variable condition;
	std::mutex mutex;
	std::thread thread;
};
}
