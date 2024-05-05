#include <nano/node/nodeconfig.hpp>
#include <nano/node/online_reps.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/store/component.hpp>
#include <nano/store/online_weight.hpp>

nano::online_reps::online_reps (nano::ledger & ledger_a, nano::node_config const & config_a) :
	ledger{ ledger_a },
	config{ config_a }
{
	if (!ledger.store.init_error ())
	{
		auto transaction (ledger.store.tx_begin_read ());
		trended_m = calculate_trend (transaction);
	}
}

void nano::online_reps::observe (nano::account const & rep_a)
{
	if (ledger.weight (rep_a) > 0)
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		auto now = std::chrono::steady_clock::now ();
		auto new_insert = reps.get<tag_account> ().erase (rep_a) == 0;
		reps.insert ({ now, rep_a });
		auto cutoff = reps.get<tag_time> ().lower_bound (now - std::chrono::seconds (config.network_params.node.weight_period));
		auto trimmed = reps.get<tag_time> ().begin () != cutoff;
		reps.get<tag_time> ().erase (reps.get<tag_time> ().begin (), cutoff);
		if (new_insert || trimmed)
		{
			online_m = calculate_online ();
		}
	}
}

void nano::online_reps::sample ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	nano::uint128_t online_l = online_m;
	lock.unlock ();
	nano::uint128_t trend_l;
	{
		auto transaction (ledger.store.tx_begin_write ({ tables::online_weight }));
		// Discard oldest entries
		while (ledger.store.online_weight.count (transaction) >= config.network_params.node.max_weight_samples)
		{
			auto oldest (ledger.store.online_weight.begin (transaction));
			debug_assert (oldest != ledger.store.online_weight.end ());
			ledger.store.online_weight.del (transaction, oldest->first);
		}
		ledger.store.online_weight.put (transaction, std::chrono::system_clock::now ().time_since_epoch ().count (), online_l);
		trend_l = calculate_trend (transaction);
	}
	lock.lock ();
	trended_m = trend_l;
}

nano::uint128_t nano::online_reps::calculate_online () const
{
	nano::uint128_t current;
	for (auto & i : reps)
	{
		current += ledger.weight (i.account);
	}
	return current;
}

nano::uint128_t nano::online_reps::calculate_trend (store::transaction & transaction_a) const
{
	std::vector<nano::uint128_t> items;
	items.reserve (config.network_params.node.max_weight_samples + 1);
	items.push_back (config.online_weight_minimum.number ());
	for (auto i (ledger.store.online_weight.begin (transaction_a)), n (ledger.store.online_weight.end ()); i != n; ++i)
	{
		items.push_back (i->second.number ());
	}
	nano::uint128_t result;
	// Pick median value for our target vote weight
	auto median_idx = items.size () / 2;
	nth_element (items.begin (), items.begin () + median_idx, items.end ());
	result = items[median_idx];
	return result;
}

nano::uint128_t nano::online_reps::trended () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return trended_m;
}

nano::uint128_t nano::online_reps::online () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return online_m;
}

nano::uint128_t nano::online_reps::delta () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	// Using a larger container to ensure maximum precision
	auto weight = static_cast<nano::uint256_t> (std::max ({ online_m, trended_m, config.online_weight_minimum.number () }));
	return ((weight * online_weight_quorum) / 100).convert_to<nano::uint128_t> ();
}

std::vector<nano::account> nano::online_reps::list ()
{
	std::vector<nano::account> result;
	nano::lock_guard<nano::mutex> lock{ mutex };
	std::for_each (reps.begin (), reps.end (), [&result] (rep_info const & info_a) { result.push_back (info_a.account); });
	return result;
}

void nano::online_reps::clear ()
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	reps.clear ();
	online_m = 0;
}

std::unique_ptr<nano::container_info_component> nano::collect_container_info (online_reps & online_reps, std::string const & name)
{
	std::size_t count;
	{
		nano::lock_guard<nano::mutex> guard{ online_reps.mutex };
		count = online_reps.reps.size ();
	}

	auto sizeof_element = sizeof (decltype (online_reps.reps)::value_type);
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info_entry{ "reps", count, sizeof_element }));
	return composite;
}
