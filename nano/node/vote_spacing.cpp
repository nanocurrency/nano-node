#include <nano/node/vote_spacing.hpp>

void nano::vote_spacing::trim (std::chrono::steady_clock::time_point now)
{
	recent.get<tag_time> ().erase (recent.get<tag_time> ().begin (), recent.get<tag_time> ().upper_bound (now - delay));
}

bool nano::vote_spacing::votable (nano::root const & root_a, nano::block_hash const & hash_a, std::chrono::steady_clock::time_point now) const
{
	for (auto range = recent.get<tag_root> ().equal_range (root_a); range.first != range.second; ++range.first)
	{
		auto & item = *range.first;
		if (hash_a == item.hash && item.time >= now - delay)
		{
			return false; // Same hash and not expired -> not votable
		}
	}
	return true; // Either different hash or expired -> votable
}

void nano::vote_spacing::flag (nano::root const & root_a, nano::block_hash const & hash_a, std::chrono::steady_clock::time_point now)
{
	trim (now);

	auto existing = recent.get<tag_root> ().find (root_a);
	if (existing != recent.end ())
	{
		// We update both timestamp and hash because we want to track which fork
		// we most recently voted for. This ensures proper spacing between votes
		// for the same block while allowing immediate votes for competing forks.
		recent.get<tag_root> ().modify (existing, [now, hash_a] (entry & entry) {
			entry.time = now;
			entry.hash = hash_a;
		});
	}
	else
	{
		recent.insert ({ root_a, now, hash_a });
	}
}

std::size_t nano::vote_spacing::size () const
{
	return recent.size ();
}
