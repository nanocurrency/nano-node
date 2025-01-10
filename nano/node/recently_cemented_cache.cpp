#include <celerix/lib/utility.hpp>
#include <celerix/node/recently_cemented_cache.hpp>

/*
 * class recently_cemented
 */

celerix::recently_cemented_cache::recently_cemented_cache (std::size_t max_size_a) :
	max_size{ max_size_a }
{
}

void celerix::recently_cemented_cache::put (const celerix::election_status & status)
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };
	cemented.push_back (status);
	if (cemented.size () > max_size)
	{
		cemented.pop_front ();
	}
}

celerix::recently_cemented_cache::queue_t celerix::recently_cemented_cache::list () const
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };
	return cemented;
}

std::size_t celerix::recently_cemented_cache::size () const
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };
	return cemented.size ();
}

celerix::container_info celerix::recently_cemented_cache::container_info () const
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };

	celerix::container_info info;
	info.put ("cemented", cemented);
	return info;
}
