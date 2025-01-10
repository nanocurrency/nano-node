#pragma once

#include <celerix/lib/locks.hpp>
#include <celerix/lib/numbers.hpp>
#include <celerix/node/fwd.hpp>

#include <boost/optional.hpp>

#include <deque>
#include <memory>
#include <mutex>

namespace celerix::scheduler
{
class buckets;

class manual final
{
	std::deque<std::tuple<std::shared_ptr<celerix::block>, boost::optional<celerix::uint128_t>, celerix::election_behavior>> queue;
	celerix::node & node;
	mutable celerix::mutex mutex;
	celerix::condition_variable condition;
	bool stopped{ false };
	std::thread thread;
	void notify ();
	bool predicate () const;
	void run ();

public:
	explicit manual (celerix::node & node);
	~manual ();

	void start ();
	void stop ();

	// Manually start an election for a block
	// Call action with confirmed block, may be different than what we started with
	void push (std::shared_ptr<celerix::block> const &, boost::optional<celerix::uint128_t> const & = boost::none);

	bool contains (celerix::block_hash const &) const;

	celerix::container_info container_info () const;
};
}
