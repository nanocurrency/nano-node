#include <nano/lib/enum_util.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/node/online_reps.hpp>
#include <nano/node/rep_tiers.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>

using namespace std::chrono_literals;

nano::rep_tiers::rep_tiers (nano::ledger & ledger_a, nano::network_params & network_params_a, nano::online_reps & online_reps_a, nano::stats & stats_a, nano::logger & logger_a) :
	ledger{ ledger_a },
	network_params{ network_params_a },
	online_reps{ online_reps_a },
	stats{ stats_a },
	logger{ logger_a }
{
}

nano::rep_tiers::~rep_tiers ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void nano::rep_tiers::start ()
{
	debug_assert (!thread.joinable ());

	thread = std::thread{ [this] () {
		nano::thread_role::set (nano::thread_role::name::rep_tiers);
		run ();
	} };
}

void nano::rep_tiers::stop ()
{
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	if (thread.joinable ())
	{
		thread.join ();
	}
}

nano::rep_tier nano::rep_tiers::tier (const nano::account & representative) const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	if (representatives_3.find (representative) != representatives_3.end ())
	{
		return nano::rep_tier::tier_3;
	}
	if (representatives_2.find (representative) != representatives_2.end ())
	{
		return nano::rep_tier::tier_2;
	}
	if (representatives_1.find (representative) != representatives_1.end ())
	{
		return nano::rep_tier::tier_1;
	}
	return nano::rep_tier::none;
}

void nano::rep_tiers::run ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		stats.inc (nano::stat::type::rep_tiers, nano::stat::detail::loop);

		lock.unlock ();

		calculate_tiers ();

		lock.lock ();

		std::chrono::milliseconds interval = network_params.network.is_dev_network () ? 500ms : 10min;
		condition.wait_for (lock, interval);
	}
}

void nano::rep_tiers::calculate_tiers ()
{
	auto stake = online_reps.trended ();
	auto rep_amounts = ledger.cache.rep_weights.get_rep_amounts ();

	decltype (representatives_1) representatives_1_l;
	decltype (representatives_2) representatives_2_l;
	decltype (representatives_3) representatives_3_l;

	int ignored = 0;
	for (auto const & rep_amount : rep_amounts)
	{
		nano::account const & representative = rep_amount.first;

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

	stats.add (nano::stat::type::rep_tiers, nano::stat::detail::processed, nano::stat::dir::in, rep_amounts.size ());
	stats.add (nano::stat::type::rep_tiers, nano::stat::detail::ignored, nano::stat::dir::in, ignored);
	logger.debug (nano::log::type::rep_tiers, "Representative tiers updated, tier 1: {}, tier 2: {}, tier 3: {} ({} ignored)",
	representatives_1_l.size (),
	representatives_2_l.size (),
	representatives_3_l.size (),
	ignored);

	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		representatives_1 = std::move (representatives_1_l);
		representatives_2 = std::move (representatives_2_l);
		representatives_3 = std::move (representatives_3_l);
	}

	stats.inc (nano::stat::type::rep_tiers, nano::stat::detail::updated);
}

std::unique_ptr<nano::container_info_component> nano::rep_tiers::collect_container_info (const std::string & name)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "representatives_1", representatives_1.size (), sizeof (decltype (representatives_1)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "representatives_2", representatives_2.size (), sizeof (decltype (representatives_2)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "representatives_3", representatives_3.size (), sizeof (decltype (representatives_3)::value_type) }));
	return composite;
}

nano::stat::detail nano::to_stat_detail (nano::rep_tier tier)
{
	return nano::enum_util::cast<nano::stat::detail> (tier);
}