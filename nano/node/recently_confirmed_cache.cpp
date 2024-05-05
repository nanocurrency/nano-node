#include <nano/lib/utility.hpp>
#include <nano/node/recently_confirmed_cache.hpp>

/*
 * class recently_confirmed
 */

nano::recently_confirmed_cache::recently_confirmed_cache (std::size_t max_size_a) :
	max_size{ max_size_a }
{
}

void nano::recently_confirmed_cache::put (const nano::qualified_root & root, const nano::block_hash & hash)
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	confirmed.get<tag_sequence> ().emplace_back (root, hash);
	if (confirmed.size () > max_size)
	{
		confirmed.get<tag_sequence> ().pop_front ();
	}
}

void nano::recently_confirmed_cache::erase (const nano::block_hash & hash)
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	confirmed.get<tag_hash> ().erase (hash);
}

void nano::recently_confirmed_cache::clear ()
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	confirmed.clear ();
}

bool nano::recently_confirmed_cache::exists (const nano::block_hash & hash) const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return confirmed.get<tag_hash> ().find (hash) != confirmed.get<tag_hash> ().end ();
}

bool nano::recently_confirmed_cache::exists (const nano::qualified_root & root) const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return confirmed.get<tag_root> ().find (root) != confirmed.get<tag_root> ().end ();
}

std::size_t nano::recently_confirmed_cache::size () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return confirmed.size ();
}

nano::recently_confirmed_cache::entry_t nano::recently_confirmed_cache::back () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return confirmed.back ();
}

std::unique_ptr<nano::container_info_component> nano::recently_confirmed_cache::collect_container_info (const std::string & name)
{
	nano::unique_lock<nano::mutex> lock{ mutex };

	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info_entry{ "confirmed", confirmed.size (), sizeof (decltype (confirmed)::value_type) }));
	return composite;
}
