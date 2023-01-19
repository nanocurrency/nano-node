#include <nano/node/election.hpp>
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

std::size_t nano::inactive_cache_information::fill (std::shared_ptr<nano::election> election) const
{
	std::size_t inserted = 0;
	for (auto const & [rep, timestamp] : voters)
	{
		auto [is_replay, processed] = election->vote (rep, timestamp, hash, nano::election::vote_source::cache);
		if (processed)
		{
			inserted++;
		}
	}
	return inserted;
}