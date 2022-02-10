#include <nano/node/inactive_cache_information.hpp>

using namespace std::chrono;

std::string nano::inactive_cache_information::to_string () const
{
	std::stringstream ss;
	ss << "hash=" << hash.to_string ();
	ss << ", arrival=" << std::chrono::duration_cast<std::chrono::seconds> (arrival.time_since_epoch ()).count ();
	ss << ", " << status.to_string ();
	ss << ", " << voters.size () << " voters";
	for (auto const & [rep, timestamp] : voters)
	{
		ss << " " << rep.to_account () << "/" << timestamp;
	}
	return ss.str ();
}
