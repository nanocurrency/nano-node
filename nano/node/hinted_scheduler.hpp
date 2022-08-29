#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/timer.hpp>
#include <nano/lib/utility.hpp>
#include <nano/secure/common.hpp>

#include <condition_variable>
#include <memory>
#include <queue>
#include <thread>
#include <vector>

namespace nano
{
class node;
class active_transactions;
class vote_cache;
class online_reps;

class hinted_scheduler final
{
public:
	explicit hinted_scheduler (nano::node &, nano::vote_cache &, nano::active_transactions &, nano::online_reps &);
	~hinted_scheduler ();

	void start ();
	void stop ();
	void notify ();

private:
	bool predicate (nano::uint128_t const & minimum_tally) const;
	void run ();
	bool run_one (nano::uint128_t const & minimum_tally);

	nano::uint128_t tally_threshold () const;

private: // Dependencies
	nano::node & node;
	nano::vote_cache & inactive_vote_cache;
	nano::active_transactions & active;
	nano::online_reps & online_reps;

private:
	bool stopped;
	nano::condition_variable condition;
	mutable nano::mutex mutex;
	std::thread thread;
};
}
