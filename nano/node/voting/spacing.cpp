#include <nano/node/voting/spacing.hpp>

void nano::voting::spacing::trim ()
{
	recent.get<tag_time> ().erase (recent.get<tag_time> ().begin (), recent.get<tag_time> ().upper_bound (std::chrono::steady_clock::now () - delay));
}

bool nano::voting::spacing::votable (nano::root const & root_a, nano::block_hash const & hash_a) const
{
	bool result = true;
	for (auto range = recent.get<tag_root> ().equal_range (root_a); result && range.first != range.second; ++range.first)
	{
		auto & item = *range.first;
		result = hash_a == item.hash || item.time < std::chrono::steady_clock::now () - delay;
	}
	return result;
}

void nano::voting::spacing::flag (nano::root const & root_a, nano::block_hash const & hash_a)
{
	trim ();
	auto now = std::chrono::steady_clock::now ();
	auto existing = recent.get<tag_root> ().find (root_a);
	if (existing != recent.end ())
	{
		recent.get<tag_root> ().modify (existing, [now] (entry & entry) {
			entry.time = now;
		});
	}
	else
	{
		recent.insert ({ root_a, now, hash_a });
	}
}

std::size_t nano::voting::spacing::size () const
{
	return recent.size ();
}
