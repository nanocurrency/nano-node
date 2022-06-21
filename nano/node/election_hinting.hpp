#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/timer.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/vote_cache.hpp>
#include <nano/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <condition_variable>
#include <memory>
#include <queue>
#include <thread>
#include <vector>

namespace mi = boost::multi_index;

namespace nano
{
class node;
class vote;
class election;

class election_hinting final
{
public:
	explicit election_hinting (nano::node & node);
	~election_hinting ();
	void stop ();
	void flush ();
	void notify ();
	std::size_t size () const;
	bool empty () const;

private:
	bool predicate () const;
	void run ();
	bool run_one ();

	nano::uint128_t tally_threshold () const;

	nano::node & node;
	bool stopped;
	nano::condition_variable condition;
	mutable nano::mutex mutex;
	std::thread thread;
};
}