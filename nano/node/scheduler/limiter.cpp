#include <nano/lib/locks.hpp>
#include <nano/lib/stats.hpp>
#include <nano/node/active_transactions.hpp>
#include <nano/node/scheduler/limiter.hpp>

#include <boost/format.hpp>

nano::scheduler::limiter::limiter (insert_t const & insert, size_t limit, nano::election_behavior behavior) :
	insert{ insert },
	limit_m{ limit },
	behavior{ behavior }
{
	debug_assert (limit > 0);
}

size_t nano::scheduler::limiter::limit () const
{
	return limit_m;
}

std::unordered_set<nano::qualified_root> nano::scheduler::limiter::elections () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return elections_m;
}

bool nano::scheduler::limiter::available () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	auto result = elections_m.size () < limit ();
	return result;
}

nano::election_insertion_result nano::scheduler::limiter::activate (std::shared_ptr<nano::block> const & block)
{
	if (!available ())
	{
		return { nullptr, false };
	}

	// This code section is not synchronous with respect to available ()
	// It is assumed 'sink' is thread safe and only one call to
	auto result = insert (block, behavior);
	if (result.inserted)
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		elections_m.insert (result.election->qualified_root);
		// Capture via weak_ptr so we don't have to consider destruction order of nano::scheduler::limiter compared to nano::election.
		result.election->destructor_observers.add ([this_w = std::weak_ptr<nano::scheduler::limiter>{ shared_from_this () }] (nano::qualified_root const & root) {
			if (auto this_l = this_w.lock ())
			{
				this_l->election_destruction_notification (root);
			}
		});
	}
	return result;
}

size_t nano::scheduler::limiter::election_destruction_notification (nano::qualified_root const & root)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return elections_m.erase (root);
}
