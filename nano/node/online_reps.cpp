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
		auto transaction = ledger.tx_begin_write (nano::store::writer::online_weight);

		sanitize_trended (transaction);
		auto trended_result = calculate_trended (transaction);

		nano::lock_guard<nano::mutex> lock{ mutex };
		cached_trended = trended_result.trended;

		logger.info (nano::log::type::online_reps, "Initial trended weight: {} (samples: {})",
		nano::uint128_union{ trended_result.trended }.format_balance (nano_ratio, 1, true),
		trended_result.samples);
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
		bool new_insert = reps.get<tag_account> ().erase (rep) == 0;
		reps.insert ({ now, rep });

		stats.inc (nano::stat::type::online_reps, new_insert ? nano::stat::detail::rep_new : nano::stat::detail::rep_update);

		if (new_insert)
		{
			logger.debug (nano::log::type::online_reps, "Observed new representative: {}", rep.to_account ());
			update_online ();
		}
	}
}

void nano::online_reps::trim ()
{
	debug_assert (!mutex.try_lock ());

	auto const now = std::chrono::steady_clock::now ();
	auto const cutoff = now - config.network_params.node.weight_interval * 2;

	while (reps.get<tag_time> ().begin () != reps.get<tag_time> ().end ())
	{
		auto oldest = reps.get<tag_time> ().begin ();
		if (oldest->time < cutoff)
		{
			stats.inc (nano::stat::type::online_reps, nano::stat::detail::rep_trim);
			logger.debug (nano::log::type::online_reps, "Removing representative: {}, last observed: {}s ago",
			oldest->account.to_account (),
			nano::log::seconds_delta (oldest->time, now));

			reps.get<tag_time> ().erase (oldest);
		}
		else
		{
			break; // Entries are ordered by timestamp, break early
		}
	}
}

void nano::online_reps::update_online ()
{
	stats.inc (nano::stat::type::online_reps, nano::stat::detail::update_online);
	cached_online = calculate_online ();
}

void nano::online_reps::run ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	last_sample = std::chrono::steady_clock::now ();
	while (!stopped)
	{
		condition.wait_for (lock, nano::is_dev_run () ? 100ms : 1s, [this] () {
			return stopped;
		});
		if (stopped)
		{
			return;
		}

		// Always recalculate online weight
		update_online ();

		// Sample trended weight if the next sample time has been reached
		auto const now = std::chrono::steady_clock::now ();
		auto const next_sample = last_sample + config.network_params.node.weight_interval;
		if (now >= next_sample)
		{
			trim ();
			lock.unlock ();
			sample ();
			lock.lock ();
			last_sample = now;
		}
	}
}

void nano::online_reps::sample ()
{
	stats.inc (nano::stat::type::online_reps, nano::stat::detail::sample);

	auto transaction = ledger.tx_begin_write (nano::store::writer::online_weight);

	// Remove old records from the database
	trim_trended (transaction);

	// Put current online weight sample into the database
	ledger.store.online_weight.put (transaction, nano::seconds_since_epoch (), online ());

	// Update current trended weight
	auto trended_result = calculate_trended (transaction);
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		cached_trended = trended_result.trended;
	}

	logger.info (nano::log::type::online_reps, "Updated trended weight: {} (samples: {})",
	nano::uint128_union{ trended_result.trended }.format_balance (nano_ratio, 1, true),
	trended_result.samples);
}

nano::uint128_t nano::online_reps::calculate_online () const
{
	debug_assert (!mutex.try_lock ());
	return std::accumulate (reps.begin (), reps.end (), nano::uint128_t{ 0 }, [this] (nano::uint128_t current, rep_info const & info) {
		return current + ledger.weight (info.account);
	});
}

void nano::online_reps::trim_trended (nano::store::write_transaction const & transaction)
{
	auto const now = std::chrono::system_clock::now ();
	auto const cutoff = now - config.network_params.node.weight_cutoff;

	std::deque<nano::store::online_weight::iterator::value_type> to_remove;

	for (auto it = ledger.store.online_weight.begin (transaction); it != ledger.store.online_weight.end (transaction); ++it)
	{
		auto tstamp = nano::from_seconds_since_epoch (it->first);
		if (tstamp < cutoff)
		{
			stats.inc (nano::stat::type::online_reps, nano::stat::detail::trim_trend);
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

void nano::online_reps::sanitize_trended (nano::store::write_transaction const & transaction)
{
	auto const now = std::chrono::system_clock::now ();
	auto const cutoff = now - config.network_params.node.weight_cutoff;

	size_t removed_old = 0, removed_future = 0;
	std::deque<nano::store::online_weight::iterator::value_type> to_remove;

	for (auto it = ledger.store.online_weight.begin (transaction); it != ledger.store.online_weight.end (transaction); ++it)
	{
		auto tstamp = nano::from_seconds_since_epoch (it->first);
		if (tstamp < cutoff)
		{
			stats.inc (nano::stat::type::online_reps, nano::stat::detail::sanitize_old);
			to_remove.push_back (*it);
			++removed_old;
		}
		else if (tstamp > now)
		{
			stats.inc (nano::stat::type::online_reps, nano::stat::detail::sanitize_future);
			to_remove.push_back (*it);
			++removed_future;
		}
	}

	// Remove entries after iterating to avoid iterator invalidation
	for (auto const & entry : to_remove)
	{
		ledger.store.online_weight.del (transaction, entry.first);
	}

	logger.log ((removed_old + removed_future) > 0 ? nano::log::level::info : nano::log::level::debug,
	nano::log::type::online_reps, "Sanitized online weight trend, remaining samples: {}, removed: {} (old: {}, future: {})",
	ledger.store.online_weight.count (transaction),
	removed_old + removed_future,
	removed_old,
	removed_future);

	// Ensure that all remaining entries are within the expected range
	debug_assert (verify_consistency (transaction, now, cutoff));
}

bool nano::online_reps::verify_consistency (nano::store::write_transaction const & transaction, std::chrono::system_clock::time_point now, std::chrono::system_clock::time_point cutoff) const
{
	for (auto it = ledger.store.online_weight.begin (transaction); it != ledger.store.online_weight.end (transaction); ++it)
	{
		auto tstamp = nano::from_seconds_since_epoch (it->first);
		if (tstamp < cutoff || tstamp > now)
		{
			return false;
		}
	}
	return true;
}

auto nano::online_reps::calculate_trended (nano::store::transaction const & transaction) const -> trended_result
{
	std::deque<nano::uint128_t> items;
	for (auto it = ledger.store.online_weight.begin (transaction); it != ledger.store.online_weight.end (transaction); ++it)
	{
		items.push_back (it->second.number ());
	}
	if (!items.empty ())
	{
		// Pick median value for our target vote weight
		auto median_idx = items.size () / 2;
		std::nth_element (items.begin (), items.begin () + median_idx, items.end ());
		auto median_value = items[median_idx];
		return {
			.trended = median_value,
			.samples = items.size ()
		};
	}
	return { 0, 0 };
}

nano::uint128_t nano::online_reps::trended () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return std::max (cached_trended, config.online_weight_minimum.number ());
}

nano::uint128_t nano::online_reps::online () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return cached_online;
}

nano::uint128_t nano::online_reps::delta () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };

	// Using a larger container to ensure maximum precision
	auto weight = static_cast<nano::uint256_t> (std::max ({ cached_online, cached_trended, config.online_weight_minimum.number () }));
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
	cached_online = 0;
}

void nano::online_reps::force_online_weight (nano::uint128_t const & online_weight)
{
	release_assert (nano::is_dev_run ());
	nano::lock_guard<nano::mutex> lock{ mutex };
	cached_online = online_weight;
	logger.debug (nano::log::type::online_reps, "Forced online weight: {}", online_weight);
}

void nano::online_reps::force_sample ()
{
	release_assert (nano::is_dev_run ());
	sample ();
	logger.debug (nano::log::type::online_reps, "Forced sample call");
}

nano::container_info nano::online_reps::container_info () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };

	nano::container_info info;
	info.put ("reps", reps);
	return info;
}
