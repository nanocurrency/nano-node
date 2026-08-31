#include <nano/lib/blocks.hpp>
#include <nano/lib/enum_util.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/vote.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/block_processor.hpp>
#include <nano/node/cementing_set.hpp>
#include <nano/node/election.hpp>
#include <nano/node/network.hpp>
#include <nano/node/node.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/online_reps.hpp>
#include <nano/node/vote_cache.hpp>
#include <nano/node/vote_generator.hpp>
#include <nano/node/vote_router.hpp>
#include <nano/secure/ledger.hpp>

using namespace std::chrono;

std::chrono::milliseconds nano::election::base_latency () const
{
	return node.network_params.network.is_dev_network () ? 25ms : 1000ms;
}

/*
 * election
 */

nano::election::election (nano::node & node_a, std::shared_ptr<nano::block> const & block_a, nano::election_behavior election_behavior_a, nano::bucket_index bucket_a, std::function<void (std::shared_ptr<nano::block> const &)> confirmation_action_a, std::function<void (nano::account const &)> vote_action_a, std::function<void (nano::qualified_root const &)> update_action_a) :
	confirmation_action (std::move (confirmation_action_a)),
	vote_action (std::move (vote_action_a)),
	update_action (std::move (update_action_a)),
	node (node_a),
	pacing ({
	.base_latency = base_latency (),
	.vote_interval = node_a.config.network_params.network.vote_broadcast_interval,
	.block_interval = node_a.config.network_params.network.block_broadcast_interval,
	}),
	ballot (block_a, [this] (nano::account const & account) { return node.ledger.weight (account); }),
	behavior_m (election_behavior_a),
	last_round{ .winner = block_a },
	height (block_a->sideband ().height),
	root (block_a->root ()),
	qualified_root (block_a->qualified_root ()),
	account (block_a->account ()),
	bucket (bucket_a)
{
}

void nano::election::confirm_once (nano::unique_lock<nano::mutex> & lock)
{
	debug_assert (lock.owns_lock ());
	debug_assert (!mutex.try_lock ());

	// The record of a sealed election is final, a repeated or late confirmation attempt is a no-op
	if (sealed_locked ())
	{
		node.stats.inc (nano::stat::type::election, nano::stat::detail::confirm_once_failed);
		lock.unlock ();
		return;
	}

	state_m = nano::election_state::confirmed;
	state_start = std::chrono::steady_clock::now ();

	election_end = std::chrono::system_clock::now (); // Timestamp as system time
	election_duration = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now () - election_start);

	auto const status_l = status_locked ();

	node.active.recently_confirmed.put (qualified_root, status_l.winner->hash (), status_l);

	auto const extended_status = extended_status_locked ();

	node.stats.inc (nano::stat::type::election, nano::stat::detail::confirm_once);
	node.logger.trace (nano::log::type::election, nano::log::detail::election_confirmed,
	nano::log::arg{ "id", id },
	nano::log::arg{ "qualified_root", qualified_root },
	nano::log::arg{ "status", extended_status });

	node.logger.debug (nano::log::type::election, "Election confirmed with winner: {} (behavior: {}, state: {}, voters: {}, blocks: {}, duration: {}ms, confirmation requests: {})",
	status_l.winner->hash (),
	to_string (behavior_m),
	to_string (state_m),
	extended_status.status.voter_count,
	extended_status.status.block_count,
	extended_status.status.election_duration.count (),
	extended_status.status.confirmation_request_count);

	node.cementing_set.add (status_l.winner->hash (), shared_from_this ());

	lock.unlock ();

	if (update_action)
	{
		node.election_workers.post ([qualified_root_l = qualified_root, update_action_l = update_action] () {
			update_action_l (qualified_root_l);
		});
	}

	if (confirmation_action)
	{
		node.election_workers.post ([status_l, confirmation_action_l = confirmation_action] () {
			confirmation_action_l (status_l.winner);
		});
	}
}

bool nano::election::valid_change (nano::election_state expected_a, nano::election_state desired_a) const
{
	switch (expected_a)
	{
		case nano::election_state::passive:
			switch (desired_a)
			{
				case nano::election_state::active:
				case nano::election_state::confirmed:
				case nano::election_state::expired_unconfirmed:
				case nano::election_state::cancelled:
					return true; // Valid
				default:
					break;
			}
			break;
		case nano::election_state::active:
			switch (desired_a)
			{
				case nano::election_state::confirmed:
				case nano::election_state::expired_unconfirmed:
				case nano::election_state::cancelled:
					return true; // Valid
				default:
					break;
			}
			break;
		case nano::election_state::confirmed:
			switch (desired_a)
			{
				case nano::election_state::expired_confirmed:
					return true; // Valid
				default:
					break;
			}
			break;
		case nano::election_state::expired_unconfirmed:
		case nano::election_state::expired_confirmed:
		case nano::election_state::cancelled:
			// No transitions are valid from these states
			break;
	}
	return false;
}

bool nano::election::state_change (nano::election_state expected_a, nano::election_state desired_a)
{
	bool result = true;
	if (valid_change (expected_a, desired_a))
	{
		if (state_m == expected_a)
		{
			state_m = desired_a;
			state_start = std::chrono::steady_clock::now ();
			result = false;

			if (update_action)
			{
				node.election_workers.post ([qualified_root_l = qualified_root, update_action_l = update_action] () {
					update_action_l (qualified_root_l);
				});
			}
		}
	}
	return result;
}

void nano::election::request_sent ()
{
	nano::lock_guard<nano::mutex> guard{ mutex };

	pacing.request_sent (std::chrono::steady_clock::now ());
	++confirmation_request_count;

	node.stats.inc (nano::stat::type::election, nano::stat::detail::confirmation_request);
	node.logger.debug (nano::log::type::election, "Sent confirmation request for root: {} (behavior: {}, state: {}, voters: {}, blocks: {}, duration: {}ms, confirmation requests: {})",
	qualified_root,
	to_string (behavior_m),
	to_string (state_m),
	ballot.voter_count (),
	ballot.block_count (),
	duration ().count (),
	confirmation_request_count.load ());
}

bool nano::election::transition_priority ()
{
	nano::lock_guard<nano::mutex> guard{ mutex };

	if (behavior_m == nano::election_behavior::priority || behavior_m == nano::election_behavior::manual)
	{
		return false;
	}

	auto const previous_behavior = behavior_m;
	behavior_m = nano::election_behavior::priority;
	pacing.reset_vote (); // Allow new outgoing votes immediately

	node.logger.debug (nano::log::type::election, "Transitioned election behavior to priority from {} for root: {} (duration: {}ms)",
	to_string (previous_behavior),
	qualified_root,
	duration ().count ());

	if (update_action)
	{
		node.election_workers.post ([qualified_root_l = qualified_root, update_action_l = update_action] () {
			update_action_l (qualified_root_l);
		});
	}

	return true;
}

bool nano::election::transition_active ()
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return !state_change (nano::election_state::passive, nano::election_state::active); // Invert since false => success
}

bool nano::election::cancel ()
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return !state_change (state_m, nano::election_state::cancelled); // Invert since false => success
}

bool nano::election::confirmed_locked () const
{
	debug_assert (!mutex.try_lock ());
	return state_m == nano::election_state::confirmed || state_m == nano::election_state::expired_confirmed;
}

bool nano::election::confirmed () const
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	return confirmed_locked ();
}

bool nano::election::sealed_locked () const
{
	debug_assert (!mutex.try_lock ());
	return state_m != nano::election_state::passive && state_m != nano::election_state::active;
}

bool nano::election::failed () const
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	return state_m == nano::election_state::expired_unconfirmed;
}

void nano::election::broadcast_sent (nano::block_hash const & winner)
{
	nano::lock_guard<nano::mutex> guard{ mutex };

	bool const initial = pacing.is_first_block ();
	pacing.block_sent (winner, std::chrono::steady_clock::now ());

	node.stats.inc (nano::stat::type::election, initial ? nano::stat::detail::broadcast_block_initial : nano::stat::detail::broadcast_block_repeat);

	node.logger.debug (nano::log::type::election, "Broadcasted current winner: {} for root: {} (behavior: {}, state: {}, voters: {}, blocks: {}, duration: {}ms)",
	winner,
	qualified_root,
	to_string (behavior_m),
	to_string (state_m),
	ballot.voter_count (),
	ballot.block_count (),
	duration ().count ());
}

nano::election_status nano::election::status_locked () const
{
	debug_assert (!mutex.try_lock ());
	return {
		.winner = ballot.winner (),
		.tally = last_round.winner_weight,
		.final_tally = last_round.final_winner_weight,
		.election_end = election_end,
		.election_duration = election_duration,
		.confirmation_request_count = confirmation_request_count,
		.vote_broadcast_count = vote_broadcast_count,
		.block_count = nano::narrow_cast<unsigned> (ballot.block_count ()),
		.voter_count = nano::narrow_cast<unsigned> (ballot.voter_count ()),
	};
}

nano::election_status nano::election::get_status () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return status_locked ();
}

nano::election_actions nano::election::tick (std::chrono::steady_clock::time_point now)
{
	nano::unique_lock<nano::mutex> lock{ mutex };

	nano::election_actions actions;
	switch (state_m)
	{
		case nano::election_state::passive:
		{
			if (base_latency () * passive_duration_factor < now - state_start)
			{
				state_change (nano::election_state::passive, nano::election_state::active);
			}
		}
		break;
		case nano::election_state::active:
		{
			broadcast_vote_locked (now);
			actions.broadcast_block = pacing.due_block (ballot.winner ()->hash (), now);
			actions.request_votes = pacing.due_request (behavior_m, now);
		}
		break;
		case nano::election_state::confirmed:
		{
			actions.cleanup = true; // Election is done and should be cleaned up
			if (pacing.due_block (ballot.winner ()->hash (), now))
			{
				actions.broadcast_block = true; // Ensure election winner is broadcasted
			}
			state_change (nano::election_state::confirmed, nano::election_state::expired_confirmed);
		}
		break;
		case nano::election_state::expired_unconfirmed:
		case nano::election_state::expired_confirmed:
		{
			debug_assert (false);
		}
		break;
		case nano::election_state::cancelled:
		{
			actions.cleanup = true; // Clean up cancelled elections immediately
			return actions;
		}
	}
	if (actions.broadcast_block || actions.request_votes)
	{
		actions.snapshot = snapshot_locked ();
	}

	if (!confirmed_locked () && time_to_live () < now - election_start)
	{
		// It is possible the election confirmed while acquiring the mutex
		// state_change returning true would indicate it
		if (!state_change (state_m, nano::election_state::expired_unconfirmed))
		{
			node.logger.trace (nano::log::type::election, nano::log::detail::election_expired,
			nano::log::arg{ "id", id },
			nano::log::arg{ "qualified_root", qualified_root },
			nano::log::arg{ "status", extended_status_locked () });

			actions.cleanup = true; // Election expired and should be cleaned up
		}
	}

	return actions;
}

std::chrono::milliseconds nano::election::time_to_live () const
{
	switch (behavior_m)
	{
		case election_behavior::manual:
		case election_behavior::priority:
			return std::chrono::milliseconds (5 * 60 * 1000);
		case election_behavior::hinted:
		case election_behavior::optimistic:
			return std::chrono::milliseconds (30 * 1000);
	}
	debug_assert (false);
	return {};
}

nano::tally_map nano::election::tally () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return ballot.tally ();
}

nano::election_ballot::round nano::election::evaluate_locked ()
{
	debug_assert (!mutex.try_lock ());

	// The record of a sealed election is final: report the last evaluated round without re-tallying, the winner never moves once sealed
	if (sealed_locked ())
	{
		return last_round;
	}

	// The switch detection below relies on last_round.winner mirroring the ballot winner between evaluations
	release_assert (last_round.winner != nullptr);
	release_assert (last_round.winner->hash () == ballot.winner ()->hash ());

	auto const round = ballot.evaluate (node.online_reps.delta ());

	if (round.winner->hash () != last_round.winner->hash ())
	{
		auto const previous_winner = last_round.winner->hash ();

		node.logger.debug (nano::log::type::election, "Winning fork changed from {} to {} for root: {} (behavior: {}, state: {}, voters: {}, blocks: {}, duration: {}ms)",
		previous_winner,
		round.winner->hash (),
		qualified_root,
		to_string (behavior_m),
		to_string (state_m),
		ballot.voter_count (),
		ballot.block_count (),
		duration ().count ());

		// The new winner might be missing from the ledger if its fork was processed first, force reprocessing
		node.block_processor.force (round.winner);
	}

	last_round = round;

	return round;
}

void nano::election::confirm_if_quorum (nano::unique_lock<nano::mutex> & lock_a)
{
	debug_assert (lock_a.owns_lock ());

	auto const round = evaluate_locked ();
	if (round.quorum)
	{
		if (!is_quorum.exchange (true) && node.is_voting ())
		{
			++vote_broadcast_count;
			node.vote_generator.vote_final (qualified_root, round.winner->hash (), bucket);
		}
		if (round.final_quorum)
		{
			// The block might get rolled back while the election is confirming, reprocess it to ensure it is present in the ledger
			node.block_processor.add (round.winner, nano::block_source::election);
			confirm_once (lock_a);
			debug_assert (!lock_a.owns_lock ());
		}
	}
}

std::shared_ptr<nano::block> nano::election::find (nano::block_hash const & hash_a) const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return ballot.find_block (hash_a);
}

nano::vote_code nano::election::vote (nano::account const & representative, uint64_t timestamp, nano::block_hash const & block_hash, nano::vote_source source)
{
	auto const weight = node.ledger.weight (representative);

	if (!node.network_params.network.is_dev_network () && weight <= node.minimum_principal_weight ())
	{
		return vote_code::indeterminate;
	}

	nano::unique_lock<nano::mutex> lock{ mutex };

	// A sealed election no longer records votes; answer as the vote router does once the election is erased
	if (sealed_locked ())
	{
		return confirmed_locked () ? vote_code::late : vote_code::indeterminate;
	}

	auto const previous_vote = ballot.find_vote (representative);

	// Only live votes from reps with a prior recorded vote can be throttled, cached votes have already waited in the vote cache
	std::chrono::seconds cooldown{ 0s };
	if (source != nano::vote_source::cache && previous_vote)
	{
		cooldown = nano::calculate_vote_cooldown (weight, node.online_reps.trended ());
	}

	switch (ballot.vote (representative, timestamp, block_hash, cooldown, std::chrono::steady_clock::now ()))
	{
		case nano::election_ballot::vote_result::replay:
			return vote_code::replay;
		case nano::election_ballot::vote_result::ignored:
			return vote_code::ignored;
		case nano::election_ballot::vote_result::accepted:
			break;
	}

	// Stop routing an unheld hash once no current vote references it
	if (previous_vote && previous_vote->hash != block_hash && !ballot.contains_block (previous_vote->hash) && !ballot.has_vote_for (previous_vote->hash))
	{
		node.vote_router.disconnect (previous_vote->hash);
	}

	node.stats.inc (nano::stat::type::election, nano::stat::detail::vote);
	node.stats.inc (nano::stat::type::election_vote, to_stat_detail (source));

	node.logger.trace (nano::log::type::election, nano::log::detail::vote_processed,
	nano::log::arg{ "id", id },
	nano::log::arg{ "qualified_root", qualified_root },
	nano::log::arg{ "representative", representative },
	nano::log::arg{ "hash", block_hash },
	nano::log::arg{ "final", nano::vote::is_final_timestamp (timestamp) },
	nano::log::arg{ "timestamp", timestamp },
	nano::log::arg{ "vote_source", source },
	nano::log::arg{ "weight", weight });

	node.logger.debug (nano::log::type::election, "Vote received for hash: {} from: {} for root: {} (final: {}, weight: {}, source: {})",
	block_hash,
	representative,
	qualified_root,
	nano::vote::is_final_timestamp (timestamp),
	weight,
	to_string (source));

	// This must execute before calculating the vote tally to ensure accurate online weight and quorum numbers are used
	if (vote_action)
	{
		vote_action (representative);
	}

	// Re-evaluate quorum with the newly recorded vote
	confirm_if_quorum (lock);

	return vote_code::vote;
}

bool nano::election::publish (std::shared_ptr<nano::block> const & block)
{
	nano::unique_lock<nano::mutex> lock{ mutex };

	// A sealed election no longer admits blocks
	if (sealed_locked ())
	{
		return false;
	}

	auto result = ballot.insert (block);

	// Only a full ballot consults the cached tally, so try the plain insert first and pay the vote cache scan and weight lookups only on rejection
	if (result.outcome == nano::election_ballot::insert_outcome::rejected)
	{
		// The ballot is full: look up the vote cache weight backing the new fork without holding the election mutex, then retry
		lock.unlock ();

		nano::uint128_t cached_tally{ 0 };
		for (auto const & vote : node.vote_cache.find (block->hash ()))
		{
			cached_tally += node.ledger.weight (vote->account);
		}

		lock.lock ();

		if (sealed_locked ())
		{
			return false;
		}

		result = ballot.insert (block, cached_tally);
	}

	switch (result.outcome)
	{
		case nano::election_ballot::insert_outcome::inserted:
		{
			node.vote_router.connect (block->hash (), shared_from_this ());
		}
		break;
		case nano::election_ballot::insert_outcome::updated:
		{
			return false; // Block was already present, only its contents were refreshed
		}
		case nano::election_ballot::insert_outcome::replaced:
		{
			// Keep routing an evicted hash only while a current vote still references it
			if (!ballot.has_vote_for (result.evicted->hash ()))
			{
				node.vote_router.disconnect (result.evicted->hash ());
			}

			// Route votes for the admitted replacement
			node.vote_router.connect (block->hash (), shared_from_this ());

			// Clear the network filter so the evicted block can be received again
			node.network.filter.clear (result.evicted);
		}
		break;
		case nano::election_ballot::insert_outcome::rejected:
		{
			// Not backed by enough weight to take part, allow receiving it again
			node.network.filter.clear (block);
			return false;
		}
	}

	// The block may be admitted with retained weight already behind it, enough to decide the election, so re-evaluate immediately
	confirm_if_quorum (lock);

	return true;
}

nano::election_snapshot nano::election::snapshot_locked () const
{
	debug_assert (!mutex.try_lock ());
	return {
		.qualified_root = qualified_root,
		.winner = ballot.winner (),
		.quorum = is_quorum.load (),
		.votes = ballot.votes (),
	};
}

nano::election_extended_status nano::election::get_extended_status () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return extended_status_locked ();
}

nano::election_extended_status nano::election::extended_status_locked () const
{
	debug_assert (!mutex.try_lock ());
	return {
		.behavior = behavior_m,
		.status = status_locked (),
		.votes = ballot.votes (),
		.blocks = ballot.blocks (),
		.tally = ballot.tally (),
	};
}

std::shared_ptr<nano::block> nano::election::winner () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return ballot.winner ();
}

std::chrono::milliseconds nano::election::duration () const
{
	return std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now () - election_start);
}

void nano::election::broadcast_vote ()
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	broadcast_vote_locked (std::chrono::steady_clock::now ());
}

void nano::election::broadcast_vote_locked (std::chrono::steady_clock::time_point now)
{
	debug_assert (!mutex.try_lock ());

	if (!node.is_voting ())
	{
		return;
	}
	// A cancelled or expired election is sealed without a decision and must not vote
	if (sealed_locked () && !confirmed_locked ())
	{
		return;
	}
	if (!pacing.due_vote (now))
	{
		return;
	}
	pacing.vote_sent (now);

	// Re-tally a live election so the outgoing vote references the current winner
	auto const round = evaluate_locked ();

	// Broadcast a final vote if reached quorum or already confirmed
	bool const is_final = confirmed_locked () || round.quorum;

	node.stats.inc (nano::stat::type::election, nano::stat::detail::broadcast_vote);
	node.stats.inc (nano::stat::type::election, is_final ? nano::stat::detail::broadcast_vote_final : nano::stat::detail::broadcast_vote_normal);
	++vote_broadcast_count;

	node.logger.trace (nano::log::type::election, nano::log::detail::broadcast_vote,
	nano::log::arg{ "id", id },
	nano::log::arg{ "qualified_root", qualified_root },
	nano::log::arg{ "winner", ballot.winner () },
	nano::log::arg{ "type", is_final ? "final" : "normal" });

	node.vote_generator.vote (qualified_root, ballot.winner ()->hash (), bucket, is_final ? nano::vote_type::final : nano::vote_type::normal);
}

void nano::election::force_confirm ()
{
	release_assert (node.network_params.network.is_dev_network ());
	nano::unique_lock<nano::mutex> lock{ mutex };
	confirm_once (lock);
}

std::unordered_set<nano::block_hash> nano::election::blocks_hashes () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	std::unordered_set<nano::block_hash> hashes;
	for (auto const & [hash, block] : ballot.blocks ())
	{
		hashes.emplace (hash);
	}
	return hashes;
}

std::unordered_map<nano::block_hash, std::shared_ptr<nano::block>> nano::election::blocks () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return ballot.blocks ();
}

std::unordered_map<nano::account, nano::vote_info> nano::election::votes () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return ballot.votes ();
}

std::vector<nano::vote_with_weight_info> nano::election::votes_with_weight () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return ballot.votes_with_weight ();
}

nano::election_behavior nano::election::behavior () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return behavior_m;
}

nano::election_state nano::election::state () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return state_m;
}

bool nano::election::contains_block (nano::block_hash const & hash) const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return ballot.contains_block (hash);
}

size_t nano::election::voter_count () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return ballot.voter_count ();
}

size_t nano::election::block_count () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return ballot.block_count ();
}

void nano::election::operator() (nano::object_stream & obs) const
{
	obs.write ("id", id);
	obs.write ("qualified_root", qualified_root);
	obs.write ("height", height);
	obs.write ("status", get_extended_status ());
}

/*
 *
 */

std::string_view nano::to_string (nano::election_behavior behavior)
{
	return nano::enum_to_string (behavior);
}

nano::stat::detail nano::to_stat_detail (nano::election_behavior behavior)
{
	return nano::enum_convert<nano::stat::detail> (behavior);
}

std::string_view nano::to_string (nano::election_state state)
{
	return nano::enum_to_string (state);
}

nano::stat::detail nano::to_stat_detail (nano::election_state state)
{
	return nano::enum_convert<nano::stat::detail> (state);
}
