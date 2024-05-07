#include <nano/lib/blocks.hpp>
#include <nano/lib/enum_util.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/confirmation_solicitor.hpp>
#include <nano/node/election.hpp>
#include <nano/node/local_vote_history.hpp>
#include <nano/node/network.hpp>
#include <nano/node/node.hpp>
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

nano::election::election (nano::node & node_a, std::shared_ptr<nano::block> const & block_a, std::function<void (std::shared_ptr<nano::block> const &)> const & confirmation_action_a, std::function<void (nano::account const &)> const & live_vote_action_a, nano::election_behavior election_behavior_a) :
	confirmation_action (confirmation_action_a),
	live_vote_action (live_vote_action_a),
	node (node_a),
	behavior_m (election_behavior_a),
	status ({ block_a, 0, 0, std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::system_clock::now ().time_since_epoch ()), std::chrono::duration_values<std::chrono::milliseconds>::zero (), 0, 1, 0, nano::election_status_type::ongoing }),
	height (block_a->sideband ().height),
	root (block_a->root ()),
	qualified_root (block_a->qualified_root ())
{
	last_votes.emplace (nano::account::null (), nano::vote_info{ std::chrono::steady_clock::now (), 0, block_a->hash () });
	last_blocks.emplace (block_a->hash (), block_a);
}

void nano::election::confirm_once (nano::unique_lock<nano::mutex> & lock_a)
{
	debug_assert (lock_a.owns_lock ());

	// This must be kept above the setting of election state, as dependent confirmed elections require up to date changes to election_winner_details
	nano::unique_lock<nano::mutex> election_winners_lk{ node.active.election_winner_details_mutex };
	auto just_confirmed = state_m != nano::election_state::confirmed;
	state_m = nano::election_state::confirmed;
	if (just_confirmed && (node.active.election_winner_details.count (status.winner->hash ()) == 0))
	{
		node.active.election_winner_details.emplace (status.winner->hash (), shared_from_this ());
		election_winners_lk.unlock ();
		status.election_end = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::system_clock::now ().time_since_epoch ());
		status.election_duration = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now () - election_start);
		status.confirmation_request_count = confirmation_request_count;
		status.block_count = nano::narrow_cast<decltype (status.block_count)> (last_blocks.size ());
		status.voter_count = nano::narrow_cast<decltype (status.voter_count)> (last_votes.size ());
		auto const status_l = status;

		node.active.recently_confirmed.put (qualified_root, status_l.winner->hash ());

		node.logger.trace (nano::log::type::election, nano::log::detail::election_confirmed,
		nano::log::arg{ "id", id },
		nano::log::arg{ "qualified_root", qualified_root },
		nano::log::arg{ "status", current_status_locked () });

		lock_a.unlock ();

		node.background ([node_l = node.shared (), status_l, confirmation_action_l = confirmation_action] () {
			node_l->process_confirmed (status_l);

			if (confirmation_action_l)
			{
				confirmation_action_l (status_l.winner);
			}
		});
	}
	else
	{
		lock_a.unlock ();
	}
}

bool nano::election::valid_change (nano::election_state expected_a, nano::election_state desired_a) const
{
	bool result = false;
	switch (expected_a)
	{
		case nano::election_state::passive:
			switch (desired_a)
			{
				case nano::election_state::active:
				case nano::election_state::confirmed:
				case nano::election_state::expired_unconfirmed:
					result = true;
					break;
				default:
					break;
			}
			break;
		case nano::election_state::active:
			switch (desired_a)
			{
				case nano::election_state::confirmed:
				case nano::election_state::expired_unconfirmed:
					result = true;
					break;
				default:
					break;
			}
			break;
		case nano::election_state::confirmed:
			switch (desired_a)
			{
				case nano::election_state::expired_confirmed:
					result = true;
					break;
				default:
					break;
			}
			break;
		case nano::election_state::expired_unconfirmed:
		case nano::election_state::expired_confirmed:
			break;
	}
	return result;
}

bool nano::election::state_change (nano::election_state expected_a, nano::election_state desired_a)
{
	bool result = true;
	if (valid_change (expected_a, desired_a))
	{
		if (state_m == expected_a)
		{
			state_m = desired_a;
			state_start = std::chrono::steady_clock::now ().time_since_epoch ();
			result = false;
		}
	}
	return result;
}

std::chrono::milliseconds nano::election::confirm_req_time () const
{
	switch (behavior ())
	{
		case election_behavior::normal:
		case election_behavior::hinted:
			return base_latency () * 5;
		case election_behavior::optimistic:
			return base_latency () * 2;
	}
	debug_assert (false);
	return {};
}

void nano::election::send_confirm_req (nano::confirmation_solicitor & solicitor_a)
{
	if (confirm_req_time () < (std::chrono::steady_clock::now () - last_req))
	{
		if (!solicitor_a.add (*this))
		{
			last_req = std::chrono::steady_clock::now ();
			++confirmation_request_count;
		}
	}
}

void nano::election::transition_active ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	state_change (nano::election_state::passive, nano::election_state::active);
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

bool nano::election::failed () const
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	return state_m == nano::election_state::expired_unconfirmed;
}

bool nano::election::broadcast_block_predicate () const
{
	debug_assert (!mutex.try_lock ());

	// Broadcast the block if enough time has passed since the last broadcast (or it's the first broadcast)
	if (last_block + node.config.network_params.network.block_broadcast_interval < std::chrono::steady_clock::now ())
	{
		return true;
	}
	// Or the current election winner has changed
	if (status.winner->hash () != last_block_hash)
	{
		return true;
	}
	return false;
}

void nano::election::broadcast_block (nano::confirmation_solicitor & solicitor_a)
{
	debug_assert (!mutex.try_lock ());

	if (broadcast_block_predicate ())
	{
		if (!solicitor_a.broadcast (*this))
		{
			node.stats.inc (nano::stat::type::election, last_block_hash.is_zero () ? nano::stat::detail::broadcast_block_initial : nano::stat::detail::broadcast_block_repeat);

			last_block = std::chrono::steady_clock::now ();
			last_block_hash = status.winner->hash ();
		}
	}
}

void nano::election::broadcast_vote ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	broadcast_vote_locked (lock);
}

nano::vote_info nano::election::get_last_vote (nano::account const & account)
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return last_votes[account];
}

void nano::election::set_last_vote (nano::account const & account, nano::vote_info vote_info)
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	last_votes[account] = vote_info;
}

nano::election_status nano::election::get_status () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return status;
}

bool nano::election::transition_time (nano::confirmation_solicitor & solicitor_a)
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	bool result = false;
	switch (state_m)
	{
		case nano::election_state::passive:
			if (base_latency () * passive_duration_factor < std::chrono::steady_clock::now ().time_since_epoch () - state_start)
			{
				state_change (nano::election_state::passive, nano::election_state::active);
			}
			break;
		case nano::election_state::active:
			broadcast_vote_locked (lock);
			broadcast_block (solicitor_a);
			send_confirm_req (solicitor_a);
			break;
		case nano::election_state::confirmed:
			result = true; // Return true to indicate this election should be cleaned up
			broadcast_block (solicitor_a); // Ensure election winner is broadcasted
			state_change (nano::election_state::confirmed, nano::election_state::expired_confirmed);
			break;
		case nano::election_state::expired_unconfirmed:
		case nano::election_state::expired_confirmed:
			debug_assert (false);
			break;
	}

	if (!confirmed_locked () && time_to_live () < std::chrono::steady_clock::now () - election_start)
	{
		// It is possible the election confirmed while acquiring the mutex
		// state_change returning true would indicate it
		if (!state_change (state_m, nano::election_state::expired_unconfirmed))
		{
			node.logger.trace (nano::log::type::election, nano::log::detail::election_expired,
			nano::log::arg{ "id", id },
			nano::log::arg{ "qualified_root", qualified_root },
			nano::log::arg{ "status", current_status_locked () });

			result = true; // Return true to indicate this election should be cleaned up
			status.type = nano::election_status_type::stopped;
		}
	}
	return result;
}

std::chrono::milliseconds nano::election::time_to_live () const
{
	switch (behavior ())
	{
		case election_behavior::normal:
			return std::chrono::milliseconds (5 * 60 * 1000);
		case election_behavior::hinted:
		case election_behavior::optimistic:
			return std::chrono::milliseconds (30 * 1000);
	}
	debug_assert (false);
	return {};
}

std::chrono::seconds nano::election::cooldown_time (nano::uint128_t weight) const
{
	auto online_stake = node.online_reps.trended ();
	if (weight > online_stake / 20) // Reps with more than 5% weight
	{
		return std::chrono::seconds{ 1 };
	}
	if (weight > online_stake / 100) // Reps with more than 1% weight
	{
		return std::chrono::seconds{ 5 };
	}
	// The rest of smaller reps
	return std::chrono::seconds{ 15 };
}

bool nano::election::have_quorum (nano::tally_t const & tally_a) const
{
	auto i (tally_a.begin ());
	++i;
	auto second (i != tally_a.end () ? i->first : 0);
	auto delta_l (node.online_reps.delta ());
	release_assert (tally_a.begin ()->first >= second);
	bool result{ (tally_a.begin ()->first - second) >= delta_l };
	return result;
}

nano::tally_t nano::election::tally () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return tally_impl ();
}

nano::tally_t nano::election::tally_impl () const
{
	std::unordered_map<nano::block_hash, nano::uint128_t> block_weights;
	std::unordered_map<nano::block_hash, nano::uint128_t> final_weights_l;
	for (auto const & [account, info] : last_votes)
	{
		auto rep_weight (node.ledger.weight (account));
		block_weights[info.hash] += rep_weight;
		if (info.timestamp == std::numeric_limits<uint64_t>::max ())
		{
			final_weights_l[info.hash] += rep_weight;
		}
	}
	last_tally = block_weights;
	nano::tally_t result;
	for (auto const & [hash, amount] : block_weights)
	{
		auto block (last_blocks.find (hash));
		if (block != last_blocks.end ())
		{
			result.emplace (amount, block->second);
		}
	}
	// Calculate final votes sum for winner
	if (!final_weights_l.empty () && !result.empty ())
	{
		auto winner_hash (result.begin ()->second->hash ());
		auto find_final (final_weights_l.find (winner_hash));
		if (find_final != final_weights_l.end ())
		{
			final_weight = find_final->second;
		}
	}
	return result;
}

void nano::election::confirm_if_quorum (nano::unique_lock<nano::mutex> & lock_a)
{
	debug_assert (lock_a.owns_lock ());
	auto tally_l (tally_impl ());
	debug_assert (!tally_l.empty ());
	auto winner (tally_l.begin ());
	auto block_l (winner->second);
	auto const & winner_hash_l (block_l->hash ());
	status.tally = winner->first;
	status.final_tally = final_weight;
	auto const & status_winner_hash_l (status.winner->hash ());
	nano::uint128_t sum (0);
	for (auto & i : tally_l)
	{
		sum += i.first;
	}
	if (sum >= node.online_reps.delta () && winner_hash_l != status_winner_hash_l)
	{
		status.winner = block_l;
		remove_votes (status_winner_hash_l);
		node.block_processor.force (block_l);
	}
	if (have_quorum (tally_l))
	{
		if (!is_quorum.exchange (true) && node.config.enable_voting && node.wallets.reps ().voting > 0)
		{
			node.final_generator.add (root, status.winner->hash ());
		}
		if (final_weight >= node.online_reps.delta ())
		{
			confirm_once (lock_a);
		}
	}
}

void nano::election::try_confirm (nano::block_hash const & hash)
{
	nano::unique_lock<nano::mutex> election_lock{ mutex };
	auto winner = status.winner;
	if (winner && winner->hash () == hash)
	{
		if (!confirmed_locked ())
		{
			confirm_once (election_lock);
		}
	}
}

std::shared_ptr<nano::block> nano::election::find (nano::block_hash const & hash_a) const
{
	std::shared_ptr<nano::block> result;
	nano::lock_guard<nano::mutex> guard{ mutex };
	if (auto existing = last_blocks.find (hash_a); existing != last_blocks.end ())
	{
		result = existing->second;
	}
	return result;
}

nano::vote_code nano::election::vote (nano::account const & rep, uint64_t timestamp_a, nano::block_hash const & block_hash_a, nano::vote_source vote_source_a)
{
	auto weight = node.ledger.weight (rep);
	if (!node.network_params.network.is_dev_network () && weight <= node.minimum_principal_weight ())
	{
		return vote_code::indeterminate;
	}

	nano::unique_lock<nano::mutex> lock{ mutex };

	auto last_vote_it (last_votes.find (rep));
	if (last_vote_it != last_votes.end ())
	{
		auto last_vote_l (last_vote_it->second);
		if (last_vote_l.timestamp > timestamp_a)
		{
			return vote_code::replay;
		}
		if (last_vote_l.timestamp == timestamp_a && !(last_vote_l.hash < block_hash_a))
		{
			return vote_code::replay;
		}

		auto max_vote = timestamp_a == std::numeric_limits<uint64_t>::max () && last_vote_l.timestamp < timestamp_a;

		bool past_cooldown = true;
		if (vote_source_a == vote_source::live) // Only cooldown live votes
		{
			const auto cooldown = cooldown_time (weight);
			past_cooldown = last_vote_l.time <= std::chrono::steady_clock::now () - cooldown;
		}

		if (!max_vote && !past_cooldown)
		{
			return vote_code::ignored;
		}
	}

	last_votes[rep] = { std::chrono::steady_clock::now (), timestamp_a, block_hash_a };
	if (vote_source_a == vote_source::live)
	{
		live_vote_action (rep);
	}

	node.stats.inc (nano::stat::type::election, vote_source_a == vote_source::live ? nano::stat::detail::vote_new : nano::stat::detail::vote_cached);
	node.logger.trace (nano::log::type::election, nano::log::detail::vote_processed,
	nano::log::arg{ "id", id },
	nano::log::arg{ "qualified_root", qualified_root },
	nano::log::arg{ "account", rep },
	nano::log::arg{ "hash", block_hash_a },
	nano::log::arg{ "final", nano::vote::is_final_timestamp (timestamp_a) },
	nano::log::arg{ "timestamp", timestamp_a },
	nano::log::arg{ "vote_source", vote_source_a },
	nano::log::arg{ "weight", weight });

	if (!confirmed_locked ())
	{
		confirm_if_quorum (lock);
	}

	return vote_code::vote;
}

bool nano::election::publish (std::shared_ptr<nano::block> const & block_a)
{
	nano::unique_lock<nano::mutex> lock{ mutex };

	// Do not insert new blocks if already confirmed
	auto result (confirmed_locked ());
	if (!result && last_blocks.size () >= max_blocks && last_blocks.find (block_a->hash ()) == last_blocks.end ())
	{
		if (!replace_by_weight (lock, block_a->hash ()))
		{
			result = true;
			node.network.publish_filter.clear (block_a);
		}
		debug_assert (lock.owns_lock ());
	}
	if (!result)
	{
		auto existing = last_blocks.find (block_a->hash ());
		if (existing == last_blocks.end ())
		{
			last_blocks.emplace (std::make_pair (block_a->hash (), block_a));
		}
		else
		{
			result = true;
			existing->second = block_a;
			if (status.winner->hash () == block_a->hash ())
			{
				status.winner = block_a;
				node.network.flood_block (block_a, nano::transport::buffer_drop_policy::no_limiter_drop);
			}
		}
	}
	/*
	Result is true if:
	1) election is confirmed or expired
	2) given election contains 10 blocks & new block didn't receive enough votes to replace existing blocks
	3) given block in already in election & election contains less than 10 blocks (replacing block content with new)
	*/
	return result;
}

nano::election_extended_status nano::election::current_status () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return current_status_locked ();
}

nano::election_extended_status nano::election::current_status_locked () const
{
	debug_assert (!mutex.try_lock ());

	nano::election_status status_l = status;
	status_l.confirmation_request_count = confirmation_request_count;
	status_l.block_count = nano::narrow_cast<decltype (status_l.block_count)> (last_blocks.size ());
	status_l.voter_count = nano::narrow_cast<decltype (status_l.voter_count)> (last_votes.size ());
	return nano::election_extended_status{ status_l, last_votes, last_blocks, tally_impl () };
}

std::shared_ptr<nano::block> nano::election::winner () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return status.winner;
}

std::chrono::milliseconds nano::election::duration () const
{
	return std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now () - election_start);
}

void nano::election::broadcast_vote_locked (nano::unique_lock<nano::mutex> & lock)
{
	debug_assert (lock.owns_lock ());

	if (std::chrono::steady_clock::now () < last_vote + node.config.network_params.network.vote_broadcast_interval)
	{
		return;
	}
	last_vote = std::chrono::steady_clock::now ();

	if (node.config.enable_voting && node.wallets.reps ().voting > 0)
	{
		node.stats.inc (nano::stat::type::election, nano::stat::detail::broadcast_vote);

		if (confirmed_locked () || have_quorum (tally_impl ()))
		{
			node.stats.inc (nano::stat::type::election, nano::stat::detail::broadcast_vote_final);
			node.logger.trace (nano::log::type::election, nano::log::detail::broadcast_vote,
			nano::log::arg{ "id", id },
			nano::log::arg{ "qualified_root", qualified_root },
			nano::log::arg{ "winner", status.winner },
			nano::log::arg{ "type", "final" });

			node.final_generator.add (root, status.winner->hash ()); // Broadcasts vote to the network
		}
		else
		{
			node.stats.inc (nano::stat::type::election, nano::stat::detail::broadcast_vote_normal);
			node.logger.trace (nano::log::type::election, nano::log::detail::broadcast_vote,
			nano::log::arg{ "id", id },
			nano::log::arg{ "qualified_root", qualified_root },
			nano::log::arg{ "winner", status.winner },
			nano::log::arg{ "type", "normal" });

			node.generator.add (root, status.winner->hash ()); // Broadcasts vote to the network
		}
	}
}

void nano::election::remove_votes (nano::block_hash const & hash_a)
{
	debug_assert (!mutex.try_lock ());
	if (node.config.enable_voting && node.wallets.reps ().voting > 0)
	{
		// Remove votes from election
		auto list_generated_votes (node.history.votes (root, hash_a));
		for (auto const & vote : list_generated_votes)
		{
			last_votes.erase (vote->account);
		}
		// Clear votes cache
		node.history.erase (root);
	}
}

void nano::election::remove_block (nano::block_hash const & hash_a)
{
	debug_assert (!mutex.try_lock ());
	if (status.winner->hash () != hash_a)
	{
		if (auto existing = last_blocks.find (hash_a); existing != last_blocks.end ())
		{
			erase_if (last_votes, [hash_a] (auto const & entry) {
				return entry.second.hash == hash_a;
			});

			node.network.publish_filter.clear (existing->second);
			last_blocks.erase (hash_a);
		}
	}
}

bool nano::election::replace_by_weight (nano::unique_lock<nano::mutex> & lock_a, nano::block_hash const & hash_a)
{
	debug_assert (lock_a.owns_lock ());
	nano::block_hash replaced_block (0);
	auto winner_hash (status.winner->hash ());
	// Sort existing blocks tally
	std::vector<std::pair<nano::block_hash, nano::uint128_t>> sorted;
	sorted.reserve (last_tally.size ());
	std::copy (last_tally.begin (), last_tally.end (), std::back_inserter (sorted));
	lock_a.unlock ();

	// Sort in ascending order
	std::sort (sorted.begin (), sorted.end (), [] (auto const & left, auto const & right) { return left.second < right.second; });

	auto votes_tally = [this] (std::vector<std::shared_ptr<nano::vote>> const & votes) {
		nano::uint128_t result{ 0 };
		for (auto const & vote : votes)
		{
			result += node.ledger.weight (vote->account);
		}
		return result;
	};

	// Replace if lowest tally is below inactive cache new block weight
	auto inactive_existing = node.vote_cache.find (hash_a);
	auto inactive_tally = votes_tally (inactive_existing);
	if (inactive_tally > 0 && sorted.size () < max_blocks)
	{
		// If count of tally items is less than 10, remove any block without tally
		for (auto const & [hash, block] : blocks ())
		{
			if (std::find_if (sorted.begin (), sorted.end (), [&hash = hash] (auto const & item_a) { return item_a.first == hash; }) == sorted.end () && hash != winner_hash)
			{
				replaced_block = hash;
				break;
			}
		}
	}
	else if (inactive_tally > 0 && inactive_tally > sorted.front ().second)
	{
		if (sorted.front ().first != winner_hash)
		{
			replaced_block = sorted.front ().first;
		}
		else if (inactive_tally > sorted[1].second)
		{
			// Avoid removing winner
			replaced_block = sorted[1].first;
		}
	}

	bool replaced (false);
	if (!replaced_block.is_zero ())
	{
		node.vote_router.disconnect (replaced_block);
		lock_a.lock ();
		remove_block (replaced_block);
		replaced = true;
	}
	else
	{
		lock_a.lock ();
	}
	return replaced;
}

void nano::election::force_confirm ()
{
	release_assert (node.network_params.network.is_dev_network ());
	nano::unique_lock<nano::mutex> lock{ mutex };
	confirm_once (lock);
}

std::unordered_map<nano::block_hash, std::shared_ptr<nano::block>> nano::election::blocks () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return last_blocks;
}

std::unordered_map<nano::account, nano::vote_info> nano::election::votes () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return last_votes;
}

std::vector<nano::vote_with_weight_info> nano::election::votes_with_weight () const
{
	std::multimap<nano::uint128_t, nano::vote_with_weight_info, std::greater<nano::uint128_t>> sorted_votes;
	std::vector<nano::vote_with_weight_info> result;
	auto votes_l (votes ());
	for (auto const & vote_l : votes_l)
	{
		if (vote_l.first != nullptr)
		{
			auto amount (node.ledger.cache.rep_weights.representation_get (vote_l.first));
			nano::vote_with_weight_info vote_info{ vote_l.first, vote_l.second.time, vote_l.second.timestamp, vote_l.second.hash, amount };
			sorted_votes.emplace (std::move (amount), vote_info);
		}
	}
	result.reserve (sorted_votes.size ());
	std::transform (sorted_votes.begin (), sorted_votes.end (), std::back_inserter (result), [] (auto const & entry) { return entry.second; });
	return result;
}

nano::election_behavior nano::election::behavior () const
{
	return behavior_m;
}

nano::election_state nano::election::state () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return state_m;
}

// TODO: Remove the need for .to_string () calls
void nano::election::operator() (nano::object_stream & obs) const
{
	obs.write ("id", id);
	obs.write ("qualified_root", qualified_root.to_string ());
	obs.write ("behavior", behavior_m);
	obs.write ("height", height);
	obs.write ("status", current_status ());
}

void nano::election_extended_status::operator() (nano::object_stream & obs) const
{
	obs.write ("winner", status.winner->hash ().to_string ());
	obs.write ("tally_amount", status.tally.to_string_dec ());
	obs.write ("final_tally_amount", status.final_tally.to_string_dec ());
	obs.write ("confirmation_request_count", status.confirmation_request_count);
	obs.write ("block_count", status.block_count);
	obs.write ("voter_count", status.voter_count);
	obs.write ("type", status.type);

	obs.write_range ("votes", votes, [] (auto const & entry, nano::object_stream & obs) {
		auto & [account, info] = entry;
		obs.write ("account", account.to_account ());
		obs.write ("hash", info.hash.to_string ());
		obs.write ("final", nano::vote::is_final_timestamp (info.timestamp));
		obs.write ("timestamp", info.timestamp);
		obs.write ("time", info.time.time_since_epoch ().count ());
	});

	obs.write_range ("blocks", blocks, [] (auto const & entry) {
		auto [hash, block] = entry;
		return block;
	});

	obs.write_range ("tally", tally, [] (auto const & entry, nano::object_stream & obs) {
		auto & [amount, block] = entry;
		obs.write ("hash", block->hash ().to_string ());
		obs.write ("amount", amount);
	});
}

/*
 *
 */

std::string_view nano::to_string (nano::election_behavior behavior)
{
	return nano::enum_util::name (behavior);
}

nano::stat::detail nano::to_stat_detail (nano::election_behavior behavior)
{
	return nano::enum_util::cast<nano::stat::detail> (behavior);
}

std::string_view nano::to_string (nano::election_state state)
{
	return nano::enum_util::name (state);
}
