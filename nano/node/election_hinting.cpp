#include <nano/node/election_hinting.hpp>
#include <nano/node/node.hpp>

nano::election_hinting::election_hinting (nano::node & node) :
	node{ node },
	stopped{ false },
	thread{ [this] () { run (); } }
{
}

nano::election_hinting::~election_hinting ()
{
	stop ();
	thread.join ();
}

void nano::election_hinting::stop ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	stopped = true;
	notify ();
}

void nano::election_hinting::flush ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	condition.wait (lock, [this] () {
		return stopped || empty () || node.active.vacancy () <= 0;
	});
}

bool nano::election_hinting::empty () const
{
	return node.inactive_vote_cache.empty ();
}

std::size_t nano::election_hinting::size () const
{
	return node.inactive_vote_cache.size ();
}

void nano::election_hinting::notify ()
{
	condition.notify_all ();
}

bool nano::election_hinting::predicate () const
{
	// Check if there is space in AEC for starting a new hinted election
	if (node.active.vacancy_hinted () > 0)
	{
		// Check if we have a potential block in hinted queue that reaches minimum voting weight threshold
		if (node.inactive_vote_cache.peek (tally_threshold ()))
		{
			return true;
		}
	}
	return false;
}

bool nano::election_hinting::run_one ()
{
	if (auto top = node.inactive_vote_cache.pop (tally_threshold ()); top)
	{
		auto hash = top->hash;
		auto transaction (node.store.tx_begin_read ());
		auto block = node.store.block.get (transaction, hash);
		if (block != nullptr)
		{
			debug_assert (block->hash () == hash);
			if (!node.block_confirmed_or_being_confirmed (transaction, hash))
			{
				auto result = node.active.insert_hinted (block);
				if (result.election)
				{
					result.election->transition_active ();
				}
				return result.inserted;
			}
		}
		else
		{
			// Missing block in ledger to start an election, request bootstrapping it
			node.bootstrap_block (transaction, hash);
		}
	}
	return false;
}

void nano::election_hinting::run ()
{
	nano::thread_role::set (nano::thread_role::name::election_hinting);
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		// Periodically wakeup for condition checking as we do not call notify when new votes arrive in cache as that happens too often (we only notify on aec vaccancy)
		condition.wait_for (lock, std::chrono::seconds (1), [this] () {
			return stopped || predicate ();
		});
		debug_assert ((std::this_thread::yield (), true)); // Introduce some random delay in debug builds
		if (!stopped)
		{
			if (predicate ())
			{
				lock.unlock ();
				run_one ();
				lock.lock ();
			}
		}
	}
}

nano::uint128_t nano::election_hinting::tally_threshold () const
{
	const auto min_tally = (node.online_reps.trended () / 100) * node.config.election_hint_weight_percent;
	return min_tally;
}
