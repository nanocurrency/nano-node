#include <nano/lib/locks.hpp>
#include <nano/lib/stats.hpp>
#include <nano/node/active_transactions.hpp>
#include <nano/node/election_occupancy.hpp>

#include <boost/format.hpp>

nano::election_occupancy::election_occupancy (nano::active_transactions & active, size_t limit, nano::election_behavior behavior) :
	active{ active },
	limit_m{ limit },
	behavior{ behavior }
{
}

size_t nano::election_occupancy::limit () const
{
	return limit_m;
}

std::unordered_set<nano::qualified_root> nano::election_occupancy::elections () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return elections_m;
}

bool nano::election_occupancy::available () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	auto result = elections_m.size () < limit ();
	return result;
}

nano::election_insertion_result nano::election_occupancy::activate (std::shared_ptr<nano::block> const & block)
{
	if (!available ())
	{
		return { nullptr, false };
	}

	// This code section is not synchronous with respect to available ()
	// It is assumed 'sink' is thread safe and only one call to
	auto result = active.insert (block, behavior);
	if (result.inserted)
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		elections_m.insert (result.election->qualified_root);
		// Capture via weak_ptr so we don't have to consider destruction order of nano::election_occupancy compared to nano::election.
		result.election->destructor_observers.add ([this_w = std::weak_ptr<nano::election_occupancy>{ shared_from_this () }] (nano::qualified_root const & root) {
			if (auto this_l = this_w.lock ())
			{
				this_l->election_destruction_notification (root);
			}
		});
	}
	return result;
}

size_t nano::election_occupancy::election_destruction_notification (nano::qualified_root const & root)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return elections_m.erase (root);
}
