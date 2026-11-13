#include <nano/lib/container_info.hpp>
#include <nano/lib/enum_util.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/bootstrap/ondemand_index.hpp>

#include <algorithm>
#include <array>
#include <tuple>

namespace
{
// Every reason, strongest first
constexpr std::array reasons{ nano::bootstrap::ondemand_index::reason::votes, nano::bootstrap::ondemand_index::reason::dependency };

// Doublings of the retry cooldown past which it stops growing
constexpr unsigned max_cooldown_doublings = 16;
}

namespace nano::bootstrap
{
ondemand_index::ondemand_index (ondemand_config const & config_a) :
	config{ config_a }
{
}

auto ondemand_index::insert (nano::block_hash const & hash, ondemand_index::reason reason, std::chrono::steady_clock::time_point now) -> insert_result
{
	auto & by_hash = entries.get<tag_hash> ();
	if (auto existing = by_hash.find (hash); existing != by_hash.end ())
	{
		if (existing->reason >= reason)
		{
			return insert_result::duplicate;
		}

		// Stronger evidence moves the block ahead in line and keeps weaker newcomers from replacing it
		by_hash.modify (existing, [reason] (entry & item) {
			item.reason = reason;
		});

		if (existing->state != entry_state::abandoned)
		{
			return insert_result::duplicate;
		}

		// Peers that lacked the block while only weak evidence spoke for it may well hold it by now
		by_hash.modify (existing, [this, now] (entry & item) {
			item.state = entry_state::waiting;
			item.due = now + config.delay;
			item.attempts = 0;
			item.tried.clear ();
		});

		return insert_result::revived;
	}

	auto result = insert_result::inserted;
	if (entries.size () >= config.max_blocks)
	{
		// Ordered by reason and then by age, so the first entry is the oldest one with the weakest reason
		auto & by_age = entries.get<tag_age> ();
		auto const victim = by_age.begin ();
		if (victim == by_age.end ())
		{
			return insert_result::overfill;
		}

		if (victim->reason > reason)
		{
			return insert_result::overfill;
		}

		by_age.erase (victim);
		result = insert_result::replaced;
	}

	entries.insert (entry{ .hash = hash, .reason = reason, .arrival = now, .due = now + config.delay });

	return result;
}

auto ondemand_index::next (ondemand_index::reason weakest, std::chrono::steady_clock::time_point now) const -> std::optional<request>
{
	auto const & by_schedule = entries.get<tag_schedule> ();
	for (auto const reason : reasons)
	{
		if (reason < weakest)
		{
			continue;
		}

		// Waiting blocks of one reason are ordered by their due time, so the first one has been due for longest
		auto const [first, last] = by_schedule.equal_range (std::make_tuple (entry_state::waiting, reason));
		if (first == last)
		{
			continue;
		}

		if (first->due > now)
		{
			continue;
		}

		return request{ first->hash, first->reason, { first->tried.begin (), first->tried.end () } };
	}

	return std::nullopt;
}

bool ondemand_index::dispatch (nano::block_hash const & hash, id_t id, nano::account const & peer, std::chrono::steady_clock::time_point now)
{
	auto & by_hash = entries.get<tag_hash> ();
	auto const existing = by_hash.find (hash);
	if (existing == by_hash.end ())
	{
		return false;
	}

	if (existing->state != entry_state::waiting)
	{
		return false;
	}

	by_hash.modify (existing, [this, id, &peer, now] (entry & item) {
		item.state = entry_state::requested;
		item.due = now + config.request_timeout;
		item.id = id;
		item.attempts += 1;
		item.tried.push_back (peer);
	});

	return true;
}

void ondemand_index::exhausted (nano::block_hash const & hash)
{
	auto & by_hash = entries.get<tag_hash> ();
	if (auto existing = by_hash.find (hash); existing != by_hash.end ())
	{
		by_hash.modify (existing, [] (entry & item) {
			item.tried.clear ();
		});
	}
}

auto ondemand_index::fail (nano::block_hash const & hash, id_t id, std::chrono::steady_clock::time_point now) -> fail_result
{
	auto const & by_hash = entries.get<tag_hash> ();
	auto const existing = by_hash.find (hash);
	if (existing == by_hash.end ())
	{
		return fail_result::stale;
	}

	if (existing->state != entry_state::requested)
	{
		return fail_result::stale;
	}

	// An answer to an earlier request, which has been counted as failed when it timed out
	if (existing->id != id)
	{
		return fail_result::stale;
	}

	return retry_or_abandon (*existing, now);
}

auto ondemand_index::timeout (std::chrono::steady_clock::time_point now) -> std::vector<failure>
{
	// Collected first, as failing a request moves its entry within the index being walked
	// Most sweeps find nothing overdue and the rest a few requests, which fit without allocating
	boost::container::small_vector<nano::block_hash, 8> overdue;
	auto const & by_schedule = entries.get<tag_schedule> ();
	for (auto const reason : reasons)
	{
		auto const [first, last] = by_schedule.equal_range (std::make_tuple (entry_state::requested, reason));
		for (auto it = first; it != last && it->due <= now; ++it)
		{
			overdue.push_back (it->hash);
		}
	}

	if (overdue.empty ())
	{
		return {};
	}

	std::vector<failure> failures;
	failures.reserve (overdue.size ());
	auto const & by_hash = entries.get<tag_hash> ();
	for (auto const & hash : overdue)
	{
		auto const existing = by_hash.find (hash);
		debug_assert (existing != by_hash.end ());
		failures.push_back ({ hash, retry_or_abandon (*existing, now) });
	}

	return failures;
}

bool ondemand_index::submit (nano::block_hash const & hash)
{
	auto & by_hash = entries.get<tag_hash> ();
	auto const existing = by_hash.find (hash);
	if (existing == by_hash.end ())
	{
		return false;
	}

	switch (existing->state)
	{
		case entry_state::waiting:
		case entry_state::requested:
		case entry_state::abandoned:
			break;
		case entry_state::submitted:
		case entry_state::held:
			return false; // A copy was handed over before
	}

	by_hash.modify (existing, [] (entry & item) {
		item.state = entry_state::submitted;
		item.id = 0;
	});

	return true;
}

auto ondemand_index::reject (nano::block_hash const & hash, std::chrono::steady_clock::time_point now) -> fail_result
{
	auto const & by_hash = entries.get<tag_hash> ();
	auto const existing = by_hash.find (hash);
	if (existing == by_hash.end ())
	{
		return fail_result::stale;
	}

	if (existing->state != entry_state::submitted)
	{
		return fail_result::stale;
	}

	return retry_or_abandon (*existing, now);
}

bool ondemand_index::hold (nano::block_hash const & hash)
{
	auto & by_hash = entries.get<tag_hash> ();
	auto const existing = by_hash.find (hash);
	if (existing == by_hash.end ())
	{
		return false;
	}

	by_hash.modify (existing, [] (entry & item) {
		item.state = entry_state::held;
		item.id = 0;
	});

	return true;
}

std::optional<std::chrono::steady_clock::time_point> ondemand_index::erase (nano::block_hash const & hash)
{
	auto & by_hash = entries.get<tag_hash> ();
	auto const existing = by_hash.find (hash);
	if (existing == by_hash.end ())
	{
		return std::nullopt;
	}

	auto const arrival = existing->arrival;
	by_hash.erase (existing);

	return arrival;
}

std::size_t ondemand_index::expire (std::chrono::steady_clock::time_point now)
{
	std::size_t expired = 0;
	auto & by_age = entries.get<tag_age> ();
	for (auto const reason : reasons)
	{
		auto [first, last] = by_age.equal_range (std::make_tuple (reason));
		while (first != last && first->arrival + config.max_age <= now)
		{
			first = by_age.erase (first);
			++expired;
		}
	}

	return expired;
}

void ondemand_index::clear ()
{
	entries.clear ();
}

auto ondemand_index::find_reason (nano::block_hash const & hash) const -> std::optional<reason>
{
	auto const & by_hash = entries.get<tag_hash> ();
	auto const existing = by_hash.find (hash);
	if (existing == by_hash.end ())
	{
		return std::nullopt;
	}

	return existing->reason;
}

bool ondemand_index::contains (nano::block_hash const & hash) const
{
	return entries.get<tag_hash> ().contains (hash);
}

std::size_t ondemand_index::size () const
{
	return entries.size ();
}

auto ondemand_index::retry_or_abandon (entry const & target, std::chrono::steady_clock::time_point now) -> fail_result
{
	auto & by_hash = entries.get<tag_hash> ();
	auto const existing = by_hash.iterator_to (target);

	if (target.attempts >= config.max_attempts)
	{
		by_hash.modify (existing, [] (entry & item) {
			item.state = entry_state::abandoned;
			item.id = 0;
		});

		return fail_result::abandoned;
	}

	// The wait doubles with every failed request
	// A copy can fail for a block that was never requested, when it answers a request made before the block was forgotten and wanted again
	auto const failures = std::max (target.attempts, 1u);
	auto const doublings = std::min (failures - 1, max_cooldown_doublings);
	auto const cooldown = config.retry_cooldown * (1u << doublings);

	by_hash.modify (existing, [now, cooldown] (entry & item) {
		item.state = entry_state::waiting;
		item.due = now + cooldown;
		item.id = 0;
	});

	return fail_result::retry;
}

nano::container_info ondemand_index::container_info () const
{
	auto const & by_schedule = entries.get<tag_schedule> ();

	nano::container_info info;
	info.put ("total", entries.size ());
	info.put ("waiting", by_schedule.count (std::make_tuple (entry_state::waiting)));
	info.put ("requested", by_schedule.count (std::make_tuple (entry_state::requested)));
	info.put ("submitted", by_schedule.count (std::make_tuple (entry_state::submitted)));
	info.put ("held", by_schedule.count (std::make_tuple (entry_state::held)));
	info.put ("abandoned", by_schedule.count (std::make_tuple (entry_state::abandoned)));
	return info;
}

/*
 *
 */

nano::stat::detail to_stat_detail (ondemand_index::insert_result result)
{
	return nano::enum_convert<nano::stat::detail> (result);
}

nano::stat::detail to_stat_detail (ondemand_index::fail_result result)
{
	return nano::enum_convert<nano::stat::detail> (result);
}
}
