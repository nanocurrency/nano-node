#pragma once

#include <celerix/lib/numbers.hpp>
#include <celerix/lib/numbers_templ.hpp>
#include <celerix/lib/utility.hpp>
#include <celerix/secure/common.hpp>

#include <memory>
#include <thread>
#include <unordered_set>

namespace celerix
{
class ledger;
class network_params;
class stats;
class logger;
class container_info_component;
class online_reps;

// Higher number means higher priority
enum class rep_tier
{
	none, // Not a principal representatives
	tier_1, // (0.1-1%) of online stake
	tier_2, // (1-5%) of online stake
	tier_3, // (> 5%) of online stake
};

celerix::stat::detail to_stat_detail (rep_tier);

class rep_tiers final
{
public:
	rep_tiers (celerix::ledger &, celerix::network_params &, celerix::online_reps &, celerix::stats &, celerix::logger &);
	~rep_tiers ();

	void start ();
	void stop ();

	/** Returns the representative tier for the account */
	celerix::rep_tier tier (celerix::account const & representative) const;

	celerix::container_info container_info () const;

private: // Dependencies
	celerix::ledger & ledger;
	celerix::network_params & network_params;
	celerix::online_reps & online_reps;
	celerix::stats & stats;
	celerix::logger & logger;

private:
	void run ();
	void calculate_tiers ();

private:
	/** Representatives levels for early prioritization */
	std::unordered_set<celerix::account> representatives_1;
	std::unordered_set<celerix::account> representatives_2;
	std::unordered_set<celerix::account> representatives_3;

	std::atomic<bool> stopped{ false };
	celerix::condition_variable condition;
	mutable celerix::mutex mutex;
	std::thread thread;
};
}
