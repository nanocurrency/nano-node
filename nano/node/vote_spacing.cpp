#include <nano/node/vote_spacing.hpp>

void nano::vote_spacing::trim (std::chrono::steady_clock::time_point now)
{
	recent.get<tag_time> ().erase (recent.get<tag_time> ().begin (), recent.get<tag_time> ().upper_bound (now - delay));
}

bool nano::vote_spacing::votable (nano::qualified_root const & root, nano::block_hash const & hash, std::chrono::steady_clock::time_point now) const
{
	if (auto it = recent.get<tag_root> ().find (root); it != recent.end ())
	{
		if (hash == it->hash && it->time >= now - delay)
		{
			return false; // Same hash and not expired -> not votable
		}
	}
	return true; // Either different hash or expired -> votable
}

void nano::vote_spacing::flag (nano::qualified_root const & root, nano::block_hash const & hash, std::chrono::steady_clock::time_point now)
{
	trim (now);

	if (auto it = recent.get<tag_root> ().find (root); it != recent.end ())
	{
		// We update both timestamp and hash because we want to track which fork
		// we most recently voted for. This ensures proper spacing between votes
		// for the same block while allowing immediate votes for competing forks.
		recent.get<tag_root> ().modify (it, [now, hash] (entry & entry) {
			entry.hash = hash;
			entry.time = now;
		});
	}
	else
	{
		recent.insert ({ root, hash, now });
	}
}

std::size_t nano::vote_spacing::size () const
{
	return recent.size ();
}
