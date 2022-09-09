#include <nano/lib/stats.hpp>
#include <nano/node/hinted_scheduler.hpp>
#include <nano/node/node.hpp>

nano::hinted_scheduler::hinted_scheduler (config const & config_a, nano::node & node_a, nano::vote_cache & inactive_vote_cache_a, nano::active_transactions & active_a, nano::online_reps & online_reps_a, nano::stat & stats_a) :
	config_m{ config_a },
	node{ node_a },
	inactive_vote_cache{ inactive_vote_cache_a },
	active{ active_a },
	online_reps{ online_reps_a },
	stats{ stats_a },
	stopped{ false }
{
}

nano::hinted_scheduler::~hinted_scheduler ()
{
	stop ();
	if (thread.joinable ()) // Ensure thread was started
	{
		thread.join ();
	}
}

void nano::hinted_scheduler::start ()
{
	debug_assert (!thread.joinable ());
	thread = std::thread{
		[this] () { run (); }
	};
}

void nano::hinted_scheduler::stop ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	stopped = true;
	notify ();
}

void nano::hinted_scheduler::notify ()
{
	condition.notify_all ();
}

bool nano::hinted_scheduler::predicate (nano::uint128_t const & minimum_tally) const
{
	// Check if there is space inside AEC for a new hinted election
	if (active.vacancy_hinted () > 0)
	{
		// Check if there is any vote cache entry surpassing our minimum vote tally threshold
		if (inactive_vote_cache.peek (minimum_tally))
		{
			return true;
		}
	}
	return false;
}

bool nano::hinted_scheduler::run_one (nano::uint128_t const & minimum_tally)
{
	if (auto top = inactive_vote_cache.pop (minimum_tally); top)
	{
		const auto hash = top->hash; // Hash of block we want to hint

		// Check if block exists
		auto block = node.block (hash);
		if (block != nullptr)
		{
			// Ensure block is not already confirmed
			if (!node.block_confirmed_or_being_confirmed (hash))
			{
				// Try to insert it into AEC as hinted election
				// We check for AEC vacancy inside our predicate
				auto result = node.active.insert_hinted (block);

				stats.inc (nano::stat::type::hinting, result.inserted ? nano::stat::detail::hinted : nano::stat::detail::insert_failed);

				return result.inserted; // Return whether block was inserted
			}
		}
		else
		{
			// Missing block in ledger to start an election
			node.bootstrap_block (hash);

			stats.inc (nano::stat::type::hinting, nano::stat::detail::missing_block);
		}
	}
	return false;
}

void nano::hinted_scheduler::run ()
{
	nano::thread_role::set (nano::thread_role::name::election_hinting);
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		// It is possible that if we are waiting long enough this tally becomes outdated due to changes in trended online weight
		// However this is only used here for hinting, election does independent tally calculation, so there is no need to ensure it's always up-to-date
		const auto minimum_tally = tally_threshold ();

		// Periodically wakeup for condition checking
		// We are not notified every time new vote arrives in inactive vote cache as that happens too often
		condition.wait_for (lock, std::chrono::milliseconds (config_m.vote_cache_check_interval_ms), [this, minimum_tally] () {
			return stopped || predicate (minimum_tally);
		});

		debug_assert ((std::this_thread::yield (), true)); // Introduce some random delay in debug builds

		if (!stopped)
		{
			// We don't need the lock when running main loop
			lock.unlock ();

			if (predicate (minimum_tally))
			{
				run_one (minimum_tally);
			}

			lock.lock ();
		}
	}
}

nano::uint128_t nano::hinted_scheduler::tally_threshold () const
{
	const auto min_tally = (online_reps.trended () / 100) * node.config.election_hint_weight_percent;
	return min_tally;
}
