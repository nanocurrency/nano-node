#include <nano/lib/config.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/lib/timer.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/online_reps.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/store/component.hpp>
#include <nano/store/online_weight.hpp>

nano::online_reps::online_reps (nano::node_config const & config_a, nano::ledger & ledger_a, nano::stats & stats_a, nano::logger & logger_a) :
	config{ config_a },
	ledger{ ledger_a },
	stats{ stats_a },
	logger{ logger_a }
{
}

nano::online_reps::~online_reps ()
{
	debug_assert (!thread.joinable ());
}

void nano::online_reps::start ()
{
	debug_assert (!thread.joinable ());

	{
		auto transaction = ledger.store.tx_begin_write ({ tables::online_weight });
		sanitize_trend (transaction);
		trended_m = calculate_trend (transaction);
		logger.debug (nano::log::type::online_reps, "Initial trended weight: {}", fmt::streamed (trended_m));
	}

	thread = std::thread ([this] () {
		nano::thread_role::set (nano::thread_role::name::online_reps);
		run ();
	});
}

void nano::online_reps::stop ()
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

void nano::online_reps::observe (nano::account const & rep)
{
	if (ledger.weight (rep) > config.representative_vote_weight_minimum)
	{
		nano::lock_guard<nano::mutex> lock{ mutex };

		auto now = std::chrono::steady_clock::now ();
		auto new_insert = reps.get<tag_account> ().erase (rep) == 0;
		reps.insert ({ now, rep });

		stats.inc (nano::stat::type::online_reps, new_insert ? nano::stat::detail::rep_new : nano::stat::detail::rep_update);

		bool trimmed = trim ();

		// Update current online weight if anything changed
		if (new_insert || trimmed)
		{
			stats.inc (nano::stat::type::online_reps, nano::stat::detail::update_online);
			online_m = calculate_online ();
		}
	}
}

bool nano::online_reps::trim ()
{
	debug_assert (!mutex.try_lock ());

	auto now = std::chrono::steady_clock::now ();
	auto cutoff = reps.get<tag_time> ().lower_bound (now - config.network_params.node.weight_interval);
	auto trimmed = reps.get<tag_time> ().begin () != cutoff;
	reps.get<tag_time> ().erase (reps.get<tag_time> ().begin (), cutoff);
	return trimmed;
}

void nano::online_reps::run ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		auto next = std::chrono::steady_clock::now () + config.network_params.node.weight_interval;
		condition.wait_until (lock, next, [this, next] {
			return stopped || std::chrono::steady_clock::now () >= next;
		});
		if (!stopped)
		{
			lock.unlock ();
			sample ();
			lock.lock ();
		}
	}
}

void nano::online_reps::sample ()
{
	stats.inc (nano::stat::type::online_reps, nano::stat::detail::sample);

	auto transaction = ledger.store.tx_begin_write ({ tables::online_weight });
	trim_trend (transaction);
	ledger.store.online_weight.put (transaction, nano::seconds_since_epoch (), online ());
	auto trended_l = calculate_trend (transaction);
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		trended_m = trended_l;
	}

	logger.debug (nano::log::type::online_reps, "Updated trended weight: {}", fmt::streamed (trended_l));
}

nano::uint128_t nano::online_reps::calculate_online () const
{
	debug_assert (!mutex.try_lock ());
	return std::accumulate (reps.begin (), reps.end (), nano::uint128_t{ 0 }, [this] (nano::uint128_t current, rep_info const & info) {
		return current + ledger.weight (info.account);
	});
}

void nano::online_reps::trim_trend (nano::store::write_transaction const & transaction)
{
	auto const now = std::chrono::system_clock::now ();
	auto const cutoff = now - config.network_params.node.weight_cutoff;

	for (auto it = ledger.store.online_weight.begin (transaction); it != ledger.store.online_weight.end (); ++it)
	{
		auto tstamp = nano::from_seconds_since_epoch (it->first);
		if (tstamp < cutoff)
		{
			stats.inc (nano::stat::type::online_reps, nano::stat::detail::trim_trend);
			ledger.store.online_weight.del (transaction, it->first);
		}
		else
		{
			// Entries are ordered by timestamp, so break early
			break;
		}
	}

	// Ensure that all remaining entries are within the expected range
	debug_assert (verify_consistency (transaction, now, cutoff));
}

void nano::online_reps::sanitize_trend (nano::store::write_transaction const & transaction)
{
	auto const now = std::chrono::system_clock::now ();
	auto const cutoff = now - config.network_params.node.weight_cutoff;

	size_t removed_old = 0, removed_future = 0;

	for (auto it = ledger.store.online_weight.begin (transaction); it != ledger.store.online_weight.end (); ++it)
	{
		auto tstamp = nano::from_seconds_since_epoch (it->first);
		if (tstamp < cutoff)
		{
			stats.inc (nano::stat::type::online_reps, nano::stat::detail::sanitize_old);
			// TODO: Ensure it's OK to delete entry with the same key as the current iterator
			ledger.store.online_weight.del (transaction, it->first);
			++removed_old;
		}
		else if (tstamp > now)
		{
			stats.inc (nano::stat::type::online_reps, nano::stat::detail::sanitize_future);
			// TODO: Ensure it's OK to delete entry with the same key as the current iterator
			ledger.store.online_weight.del (transaction, it->first);
			++removed_future;
		}
	}

	logger.info (nano::log::type::online_reps, "Sanitized online weight trend, remaining entries: {}, removed: {} (old: {}, future: {})",
	ledger.store.online_weight.count (transaction),
	removed_old + removed_future,
	removed_old,
	removed_future);

	// Ensure that all remaining entries are within the expected range
	debug_assert (verify_consistency (transaction, now, cutoff));
}

bool nano::online_reps::verify_consistency (nano::store::write_transaction const & transaction, std::chrono::system_clock::time_point now, std::chrono::system_clock::time_point cutoff) const
{
	for (auto it = ledger.store.online_weight.begin (transaction); it != ledger.store.online_weight.end (); ++it)
	{
		auto tstamp = nano::from_seconds_since_epoch (it->first);
		if (tstamp < cutoff || tstamp > now)
		{
			return false;
		}
	}
	return true;
}

nano::uint128_t nano::online_reps::calculate_trend (store::transaction const & transaction) const
{
	std::vector<nano::uint128_t> items;
	for (auto it = ledger.store.online_weight.begin (transaction); it != ledger.store.online_weight.end (); ++it)
	{
		items.push_back (it->second.number ());
	}
	if (!items.empty ())
	{
		// Pick median value for our target vote weight
		auto median_idx = items.size () / 2;
		std::nth_element (items.begin (), items.begin () + median_idx, items.end ());
		return items[median_idx];
	}
	return 0;
}

nano::uint128_t nano::online_reps::trended () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return std::max (trended_m, config.online_weight_minimum.number ());
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
	auto delta = ((weight * online_weight_quorum) / 100).convert_to<nano::uint128_t> ();
	release_assert (delta >= config.online_weight_minimum.number () / 100 * online_weight_quorum);
	return delta;
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

void nano::online_reps::force_online_weight (nano::uint128_t const & online_weight)
{
	release_assert (nano::is_dev_run ());
	nano::lock_guard<nano::mutex> lock{ mutex };
	online_m = online_weight;
}

void nano::online_reps::force_sample ()
{
	release_assert (nano::is_dev_run ());
	sample ();
}

std::unique_ptr<nano::container_info_component> nano::online_reps::collect_container_info (std::string const & name)
{
	nano::lock_guard<nano::mutex> guard{ mutex };

	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "reps", reps.size (), sizeof (decltype (reps)::value_type) }));
	return composite;
}
