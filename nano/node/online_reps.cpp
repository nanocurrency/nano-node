#include <celerix/lib/config.hpp>
#include <celerix/lib/thread_roles.hpp>
#include <celerix/lib/timer.hpp>
#include <celerix/node/nodeconfig.hpp>
#include <celerix/node/online_reps.hpp>
#include <celerix/secure/ledger.hpp>
#include <celerix/store/component.hpp>
#include <celerix/store/online_weight.hpp>

celerix::online_reps::online_reps (celerix::node_config const & config_a, celerix::ledger & ledger_a, celerix::stats & stats_a, celerix::logger & logger_a) :
	config{ config_a },
	ledger{ ledger_a },
	stats{ stats_a },
	logger{ logger_a }
{
}

celerix::online_reps::~online_reps ()
{
	debug_assert (!thread.joinable ());
}

void celerix::online_reps::start ()
{
	debug_assert (!thread.joinable ());

	{
		auto transaction = ledger.tx_begin_write (celerix::store::writer::online_weight);
		sanitize_trended (transaction);

		auto trended_l = calculate_trended (transaction);
		celerix::lock_guard<celerix::mutex> lock{ mutex };
		cached_trended = trended_l;

		logger.info (celerix::log::type::online_reps, "Initial trended weight: {}", fmt::streamed (cached_trended));
	}

	thread = std::thread ([this] () {
		celerix::thread_role::set (celerix::thread_role::name::online_reps);
		run ();
	});
}

void celerix::online_reps::stop ()
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

void celerix::online_reps::observe (celerix::account const & rep)
{
	if (ledger.weight (rep) > config.representative_vote_weight_minimum)
	{
		celerix::lock_guard<celerix::mutex> lock{ mutex };

		auto now = std::chrono::steady_clock::now ();
		auto new_insert = reps.get<tag_account> ().erase (rep) == 0;
		reps.insert ({ now, rep });

		stats.inc (celerix::stat::type::online_reps, new_insert ? celerix::stat::detail::rep_new : celerix::stat::detail::rep_update);

		bool trimmed = trim ();

		// Update current online weight if anything changed
		if (new_insert || trimmed)
		{
			stats.inc (celerix::stat::type::online_reps, celerix::stat::detail::update_online);
			cached_online = calculate_online ();
		}
	}
}

bool celerix::online_reps::trim ()
{
	debug_assert (!mutex.try_lock ());

	auto now = std::chrono::steady_clock::now ();
	auto cutoff = reps.get<tag_time> ().lower_bound (now - config.network_params.node.weight_interval);
	auto trimmed = reps.get<tag_time> ().begin () != cutoff;
	reps.get<tag_time> ().erase (reps.get<tag_time> ().begin (), cutoff);
	return trimmed;
}

void celerix::online_reps::run ()
{
	celerix::unique_lock<celerix::mutex> lock{ mutex };
	while (!stopped)
	{
		// Set next time point explicitly to ensure that we don't sample too early
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

void celerix::online_reps::sample ()
{
	stats.inc (celerix::stat::type::online_reps, celerix::stat::detail::sample);

	auto transaction = ledger.tx_begin_write (celerix::store::writer::online_weight);

	// Remove old records from the database
	trim_trended (transaction);

	// Put current online weight sample into the database
	ledger.store.online_weight.put (transaction, celerix::seconds_since_epoch (), online ());

	// Update current trended weight
	auto trended_l = calculate_trended (transaction);
	{
		celerix::lock_guard<celerix::mutex> lock{ mutex };
		cached_trended = trended_l;
	}
	logger.info (celerix::log::type::online_reps, "Updated trended weight: {}", fmt::streamed (trended_l));
}

celerix::uint128_t celerix::online_reps::calculate_online () const
{
	debug_assert (!mutex.try_lock ());
	return std::accumulate (reps.begin (), reps.end (), celerix::uint128_t{ 0 }, [this] (celerix::uint128_t current, rep_info const & info) {
		return current + ledger.weight (info.account);
	});
}

void celerix::online_reps::trim_trended (celerix::store::write_transaction const & transaction)
{
	auto const now = std::chrono::system_clock::now ();
	auto const cutoff = now - config.network_params.node.weight_cutoff;

	std::deque<celerix::store::online_weight::iterator::value_type> to_remove;

	for (auto it = ledger.store.online_weight.begin (transaction); it != ledger.store.online_weight.end (transaction); ++it)
	{
		auto tstamp = celerix::from_seconds_since_epoch (it->first);
		if (tstamp < cutoff)
		{
			stats.inc (celerix::stat::type::online_reps, celerix::stat::detail::trim_trend);
			to_remove.push_back (*it);
		}
		else
		{
			break; // Entries are ordered by timestamp, so break early
		}
	}

	// Remove entries after iterating to avoid iterator invalidation
	for (auto const & entry : to_remove)
	{
		ledger.store.online_weight.del (transaction, entry.first);
	}

	// Ensure that all remaining entries are within the expected range
	debug_assert (verify_consistency (transaction, now, cutoff));
}

void celerix::online_reps::sanitize_trended (celerix::store::write_transaction const & transaction)
{
	auto const now = std::chrono::system_clock::now ();
	auto const cutoff = now - config.network_params.node.weight_cutoff;

	size_t removed_old = 0, removed_future = 0;
	std::deque<celerix::store::online_weight::iterator::value_type> to_remove;

	for (auto it = ledger.store.online_weight.begin (transaction); it != ledger.store.online_weight.end (transaction); ++it)
	{
		auto tstamp = celerix::from_seconds_since_epoch (it->first);
		if (tstamp < cutoff)
		{
			stats.inc (celerix::stat::type::online_reps, celerix::stat::detail::sanitize_old);
			to_remove.push_back (*it);
			++removed_old;
		}
		else if (tstamp > now)
		{
			stats.inc (celerix::stat::type::online_reps, celerix::stat::detail::sanitize_future);
			to_remove.push_back (*it);
			++removed_future;
		}
	}

	// Remove entries after iterating to avoid iterator invalidation
	for (auto const & entry : to_remove)
	{
		ledger.store.online_weight.del (transaction, entry.first);
	}

	logger.debug (celerix::log::type::online_reps, "Sanitized online weight trend, remaining entries: {}, removed: {} (old: {}, future: {})",
	ledger.store.online_weight.count (transaction),
	removed_old + removed_future,
	removed_old,
	removed_future);

	// Ensure that all remaining entries are within the expected range
	debug_assert (verify_consistency (transaction, now, cutoff));
}

bool celerix::online_reps::verify_consistency (celerix::store::write_transaction const & transaction, std::chrono::system_clock::time_point now, std::chrono::system_clock::time_point cutoff) const
{
	for (auto it = ledger.store.online_weight.begin (transaction); it != ledger.store.online_weight.end (transaction); ++it)
	{
		auto tstamp = celerix::from_seconds_since_epoch (it->first);
		if (tstamp < cutoff || tstamp > now)
		{
			return false;
		}
	}
	return true;
}

celerix::uint128_t celerix::online_reps::calculate_trended (celerix::store::transaction const & transaction) const
{
	std::vector<celerix::uint128_t> items;
	for (auto it = ledger.store.online_weight.begin (transaction); it != ledger.store.online_weight.end (transaction); ++it)
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

celerix::uint128_t celerix::online_reps::trended () const
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	return std::max (cached_trended, config.online_weight_minimum.number ());
}

celerix::uint128_t celerix::online_reps::online () const
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	return cached_online;
}

celerix::uint128_t celerix::online_reps::delta () const
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };

	// Using a larger container to ensure maximum precision
	auto weight = static_cast<celerix::uint256_t> (std::max ({ cached_online, cached_trended, config.online_weight_minimum.number () }));
	auto delta = ((weight * online_weight_quorum) / 100).convert_to<celerix::uint128_t> ();
	release_assert (delta >= config.online_weight_minimum.number () / 100 * online_weight_quorum);
	return delta;
}

std::vector<celerix::account> celerix::online_reps::list ()
{
	std::vector<celerix::account> result;
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	std::for_each (reps.begin (), reps.end (), [&result] (rep_info const & info_a) { result.push_back (info_a.account); });
	return result;
}

void celerix::online_reps::clear ()
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	reps.clear ();
	cached_online = 0;
}

void celerix::online_reps::force_online_weight (celerix::uint128_t const & online_weight)
{
	release_assert (celerix::is_dev_run ());
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	cached_online = online_weight;
}

void celerix::online_reps::force_sample ()
{
	release_assert (celerix::is_dev_run ());
	sample ();
}

celerix::container_info celerix::online_reps::container_info () const
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };

	celerix::container_info info;
	info.put ("reps", reps);
	return info;
}
