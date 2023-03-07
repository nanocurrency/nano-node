#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/secure/common.hpp>

#include <condition_variable>
#include <thread>

namespace nano
{
class active_transactions;
class election_occupancy;
class node;
class online_reps;
class vote_cache;

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
	size_t limit () const;

private:
	bool predicate (nano::uint128_t const & minimum_tally) const;
	void run ();
	bool run_one (nano::uint128_t const & minimum_tally);
	nano::election_insertion_result insert_action (std::shared_ptr<nano::block> block);

	nano::uint128_t tally_threshold () const;

private: // Dependencies
	nano::node & node;
	nano::vote_cache & inactive_vote_cache;
	std::shared_ptr<nano::election_occupancy> elections;
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
