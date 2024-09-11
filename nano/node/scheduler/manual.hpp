#pragma once
#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>

#include <boost/optional.hpp>

#include <deque>
#include <memory>
#include <mutex>

namespace nano
{
class block;
enum class election_behavior;
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

	// Manually start an election for a block
	// Call action with confirmed block, may be different than what we started with
	void push (std::shared_ptr<nano::block> const &, boost::optional<nano::uint128_t> const & = boost::none);

	std::unique_ptr<container_info_component> collect_container_info (std::string const & name) const;
}; // class manual
} // nano::scheduler
