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
class active_transactions;
class election;
class node;
class online_reps;
class store;
class vote;
class vote_cache;

class election_hinting final
{
public: // Config
	struct config
	{
		unsigned election_hint_weight_percent;
	};

public:
	explicit election_hinting (node &, const config &, vote_cache &, active_transactions &, store &, online_reps &);
	~election_hinting ();
	void stop ();
	void flush ();
	void notify ();
	std::size_t size () const;
	bool empty () const;

private:
	bool predicate (nano::uint128_t minimum_tally) const;
	void run ();
	bool run_one (nano::uint128_t minimum_tally);

	nano::uint128_t tally_threshold () const;

	bool stopped;
	nano::condition_variable condition;
	mutable nano::mutex mutex;
	std::thread thread;

	const config config_m;

private: // Dependencies
	node & node_m;
	vote_cache & vote_cache_m;
	active_transactions & active_m;
	store & store_m;
	online_reps & online_reps_m;
};
}