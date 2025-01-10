#pragma once

#include <celerix/lib/locks.hpp>
#include <celerix/lib/numbers.hpp>
#include <celerix/node/fwd.hpp>
#include <celerix/secure/common.hpp>
#include <celerix/store/transaction.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <thread>

namespace mi = boost::multi_index;

namespace celerix::scheduler
{
class hinted_config final
{
public:
	explicit hinted_config (celerix::network_constants const &);

	celerix::error deserialize (celerix::tomlconfig & toml);
	celerix::error serialize (celerix::tomlconfig & toml) const;

public:
	bool enable{ true };
	std::chrono::milliseconds check_interval{ 1000 };
	std::chrono::milliseconds block_cooldown{ 10000 };
	unsigned hinting_threshold_percent{ 10 };
	unsigned vacancy_threshold_percent{ 20 };
};

/*
 * Monitors inactive vote cache and schedules elections with the highest observed vote tally.
 */
class hinted final
{
public:
	hinted (hinted_config const &, celerix::node &, celerix::vote_cache &, celerix::active_elections &, celerix::online_reps &, celerix::stats &);
	~hinted ();

	void start ();
	void stop ();

	/*
	 * Notify about changes in AEC vacancy
	 */
	void notify ();

	celerix::container_info container_info () const;

private:
	bool predicate () const;
	void run ();
	void run_iterative ();
	void activate (secure::read_transaction &, celerix::block_hash const & hash, bool check_dependents);

	celerix::uint128_t tally_threshold () const;
	celerix::uint128_t final_tally_threshold () const;

private: // Dependencies
	celerix::node & node;
	celerix::vote_cache & vote_cache;
	celerix::active_elections & active;
	celerix::online_reps & online_reps;
	celerix::stats & stats;

private:
	hinted_config const & config;

	std::atomic<bool> stopped{ false };
	celerix::condition_variable condition;
	mutable celerix::mutex mutex;
	std::thread thread;

private:
	bool cooldown (celerix::block_hash const & hash);

	struct cooldown_entry
	{
		celerix::block_hash hash;
		std::chrono::steady_clock::time_point timeout;
	};

	// clang-format off
	class tag_hash {};
	class tag_timeout {};
	// clang-format on

	// clang-format off
	using ordered_cooldowns = boost::multi_index_container<cooldown_entry,
	mi::indexed_by<
		mi::hashed_unique<mi::tag<tag_hash>,
			mi::member<cooldown_entry, celerix::block_hash, &cooldown_entry::hash>>,
		mi::ordered_non_unique<mi::tag<tag_timeout>,
			mi::member<cooldown_entry, std::chrono::steady_clock::time_point, &cooldown_entry::timeout>>
	>>;
	// clang-format on

	ordered_cooldowns cooldowns_m;
};
}
