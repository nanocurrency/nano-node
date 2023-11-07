#pragma once
#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/node/active_transactions.hpp>

#include <boost/optional.hpp>

#include <deque>
#include <memory>
#include <mutex>

namespace nano
{
class block;
class node;
}

namespace nano::scheduler
{
class buckets;
class manual final
{
	std::deque<std::tuple<std::shared_ptr<nano::block>, boost::optional<nano::uint128_t>, nano::election_behavior>> queue;
	nano::node & node;
	mutable nano::mutex mutex;
	nano::condition_variable condition;
	bool stopped{ false };
	std::thread thread;
	void notify ();
	bool predicate () const;
	void run ();

public:
	manual (nano::node & node);
	~manual ();

	void start ();
	void stop ();

	// Manualy start an election for a block
	// Call action with confirmed block, may be different than what we started with
	void push (std::shared_ptr<nano::block> const &, boost::optional<nano::uint128_t> const & = boost::none, nano::election_behavior = nano::election_behavior::normal);

	std::unique_ptr<container_info_component> collect_container_info (std::string const & name) const;
}; // class manual
} // nano::scheduler
