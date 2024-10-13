#include <nano/lib/utility.hpp>
#include <nano/node/recently_cemented_cache.hpp>

/*
 * class recently_cemented
 */

nano::recently_cemented_cache::recently_cemented_cache (std::size_t max_size_a) :
	max_size{ max_size_a }
{
}

void nano::recently_cemented_cache::put (const nano::election_status & status)
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	cemented.push_back (status);
	if (cemented.size () > max_size)
	{
		cemented.pop_front ();
	}
}

nano::recently_cemented_cache::queue_t nano::recently_cemented_cache::list () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return cemented;
}

std::size_t nano::recently_cemented_cache::size () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return cemented.size ();
}

nano::container_info nano::recently_cemented_cache::container_info () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };

	nano::container_info info;
	info.put ("cemented", cemented);
	return info;
}
