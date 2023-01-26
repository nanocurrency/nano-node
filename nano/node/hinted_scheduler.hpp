#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/secure/common.hpp>

#include <condition_variable>
#include <thread>

namespace nano
{
class node;
class active_transactions;
class vote_cache;
class online_reps;

/*
 * Monitors inactive vote cache and schedules elections with the highest observed vote tally.
 */
class hinted_scheduler final
{
public: // Config
	struct config final
	{
		// Interval of wakeup to check inactive vote cache when idle
		uint64_t vote_cache_check_interval_ms;
	};

public:
	hinted_scheduler (config const &, nano::node &, nano::vote_cache &, nano::active_transactions &, nano::online_reps &, nano::stats &);
	~hinted_scheduler ();

	void start ();
	void stop ();

	/*
	 * Notify about changes in AEC vacancy
	 */
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
	nano::stats & stats;

private:
	config const config_m;

	bool stopped{ false };
	nano::condition_variable condition;
	mutable nano::mutex mutex;
	std::thread thread;
};
}
