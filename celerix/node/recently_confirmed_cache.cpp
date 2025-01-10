#include <celerix/lib/utility.hpp>
#include <celerix/node/recently_confirmed_cache.hpp>

/*
 * class recently_confirmed
 */

celerix::recently_confirmed_cache::recently_confirmed_cache (std::size_t max_size_a) :
	max_size{ max_size_a }
{
}

void celerix::recently_confirmed_cache::put (const celerix::qualified_root & root, const celerix::block_hash & hash)
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };
	confirmed.get<tag_sequence> ().emplace_back (root, hash);
	if (confirmed.size () > max_size)
	{
		confirmed.get<tag_sequence> ().pop_front ();
	}
}

void celerix::recently_confirmed_cache::erase (const celerix::block_hash & hash)
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };
	confirmed.get<tag_hash> ().erase (hash);
}

void celerix::recently_confirmed_cache::clear ()
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };
	confirmed.clear ();
}

bool celerix::recently_confirmed_cache::exists (const celerix::block_hash & hash) const
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };
	return confirmed.get<tag_hash> ().find (hash) != confirmed.get<tag_hash> ().end ();
}

bool celerix::recently_confirmed_cache::exists (const celerix::qualified_root & root) const
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };
	return confirmed.get<tag_root> ().find (root) != confirmed.get<tag_root> ().end ();
}

std::size_t celerix::recently_confirmed_cache::size () const
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };
	return confirmed.size ();
}

celerix::recently_confirmed_cache::entry_t celerix::recently_confirmed_cache::back () const
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };
	return confirmed.back ();
}

celerix::container_info celerix::recently_confirmed_cache::container_info () const
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };

	celerix::container_info info;
	info.put ("confirmed", confirmed);
	return info;
}
