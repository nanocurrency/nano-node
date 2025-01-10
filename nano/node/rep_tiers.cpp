#include <celerix/lib/enum_util.hpp>
#include <celerix/lib/logging.hpp>
#include <celerix/lib/stats.hpp>
#include <celerix/lib/thread_roles.hpp>
#include <celerix/node/online_reps.hpp>
#include <celerix/node/rep_tiers.hpp>
#include <celerix/secure/common.hpp>
#include <celerix/secure/ledger.hpp>

using namespace std::chrono_literals;

celerix::rep_tiers::rep_tiers (celerix::ledger & ledger_a, celerix::network_params & network_params_a, celerix::online_reps & online_reps_a, celerix::stats & stats_a, celerix::logger & logger_a) :
	ledger{ ledger_a },
	network_params{ network_params_a },
	online_reps{ online_reps_a },
	stats{ stats_a },
	logger{ logger_a }
{
}

celerix::rep_tiers::~rep_tiers ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void celerix::rep_tiers::start ()
{
	debug_assert (!thread.joinable ());

	thread = std::thread{ [this] () {
		celerix::thread_role::set (celerix::thread_role::name::rep_tiers);
		run ();
	} };
}

void celerix::rep_tiers::stop ()
{
	{
		celerix::lock_guard<celerix::mutex> lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	if (thread.joinable ())
	{
		thread.join ();
	}
}

celerix::rep_tier celerix::rep_tiers::tier (const celerix::account & representative) const
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	if (representatives_3.find (representative) != representatives_3.end ())
	{
		return celerix::rep_tier::tier_3;
	}
	if (representatives_2.find (representative) != representatives_2.end ())
	{
		return celerix::rep_tier::tier_2;
	}
	if (representatives_1.find (representative) != representatives_1.end ())
	{
		return celerix::rep_tier::tier_1;
	}
	return celerix::rep_tier::none;
}

void celerix::rep_tiers::run ()
{
	celerix::unique_lock<celerix::mutex> lock{ mutex };
	while (!stopped)
	{
		stats.inc (celerix::stat::type::rep_tiers, celerix::stat::detail::loop);

		lock.unlock ();

		calculate_tiers ();

		lock.lock ();

		std::chrono::milliseconds interval = network_params.network.is_dev_network () ? 500ms : 10min;
		condition.wait_for (lock, interval);
	}
}

void celerix::rep_tiers::calculate_tiers ()
{
	auto stake = online_reps.trended ();
	auto rep_amounts = ledger.cache.rep_weights.get_rep_amounts ();

	decltype (representatives_1) representatives_1_l;
	decltype (representatives_2) representatives_2_l;
	decltype (representatives_3) representatives_3_l;

	int ignored = 0;
	for (auto const & rep_amount : rep_amounts)
	{
		celerix::account const & representative = rep_amount.first;

		// Using ledger weight here because it takes preconfigured bootstrap weights into account
		auto weight = ledger.weight (representative);
		if (weight > stake / 1000) // 0.1% or above (level 1)
		{
			representatives_1_l.insert (representative);
			if (weight > stake / 100) // 1% or above (level 2)
			{
				representatives_2_l.insert (representative);
				if (weight > stake / 20) // 5% or above (level 3)
				{
					representatives_3_l.insert (representative);
				}
			}
		}
		else
		{
			++ignored;
		}
	}

	stats.add (celerix::stat::type::rep_tiers, celerix::stat::detail::processed, celerix::stat::dir::in, rep_amounts.size ());
	stats.add (celerix::stat::type::rep_tiers, celerix::stat::detail::ignored, celerix::stat::dir::in, ignored);

	logger.debug (celerix::log::type::rep_tiers, "Representative tiers updated, tier 1: {}, tier 2: {}, tier 3: {} ({} ignored)",
	representatives_1_l.size (),
	representatives_2_l.size (),
	representatives_3_l.size (),
	ignored);

	{
		celerix::lock_guard<celerix::mutex> guard{ mutex };
		representatives_1 = std::move (representatives_1_l);
		representatives_2 = std::move (representatives_2_l);
		representatives_3 = std::move (representatives_3_l);
	}

	stats.inc (celerix::stat::type::rep_tiers, celerix::stat::detail::updated);
}

celerix::container_info celerix::rep_tiers::container_info () const
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };

	celerix::container_info info;
	info.put ("tier_1", representatives_1);
	info.put ("tier_2", representatives_2);
	info.put ("tier_3", representatives_3);
	return info;
}

celerix::stat::detail celerix::to_stat_detail (celerix::rep_tier tier)
{
	return celerix::enum_util::cast<celerix::stat::detail> (tier);
}
