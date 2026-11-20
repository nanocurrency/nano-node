#include <nano/lib/blocks.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/tomlconfig.hpp>
#include <nano/messages/confirm.hpp>
#include <nano/messages/publish.hpp>
#include <nano/messages/vote_relay.hpp>
#include <nano/node/election.hpp>
#include <nano/node/online_reps.hpp>
#include <nano/node/rep_tiers.hpp>
#include <nano/node/repcrawler.hpp>
#include <nano/node/transport/channel.hpp>
#include <nano/node/transport/formatting.hpp>
#include <nano/node/transport/traffic_type.hpp>
#include <nano/node/vote_relay_client.hpp>
#include <nano/node/vote_solicitor.hpp>
#include <nano/secure/ledger.hpp>

#include <algorithm>
#include <iterator>
#include <limits>
#include <tuple>
#include <unordered_set>

/*
 * relay_rep_selector
 */

nano::relay_rep_selector::relay_rep_selector (relay_rep_limits const & limits_a) :
	limits{ limits_a }
{
}

std::deque<nano::account> nano::relay_rep_selector::select (std::deque<candidate> const & candidates, std::chrono::steady_clock::time_point now)
{
	// Forget reps that are no longer candidates, they became reachable or lost their weight
	std::unordered_set<nano::account> current;
	for (auto const & candidate : candidates)
	{
		current.insert (candidate.account);
	}
	std::erase_if (last_asked, [&current] (auto const & entry) {
		return !current.contains (entry.first);
	});

	std::deque<candidate const *> observed;
	std::deque<candidate const *> unobserved;
	for (auto const & candidate : candidates)
	{
		(candidate.observed ? observed : unobserved).push_back (&candidate);
	}

	// Heaviest first, the account breaks ties so the order is deterministic
	auto heavier = [] (candidate const * a, candidate const * b) {
		return std::tie (b->weight, a->account) < std::tie (a->weight, b->account);
	};
	auto asked_at = [this] (candidate const * candidate) {
		auto const it = last_asked.find (candidate->account);
		return it != last_asked.end () ? it->second : std::chrono::steady_clock::time_point{};
	};

	// Observed reps decide quorum, so the heaviest missing ones are asked first
	std::sort (observed.begin (), observed.end (), heavier);
	// Unobserved reps take turns, the longest unasked first and the heaviest among those
	std::sort (unobserved.begin (), unobserved.end (), [&] (candidate const * a, candidate const * b) {
		auto const a_asked = asked_at (a);
		auto const b_asked = asked_at (b);
		return a_asked != b_asked ? a_asked < b_asked : heavier (a, b);
	});

	std::deque<nano::account> result;
	auto take = [&] (std::deque<candidate const *> const & ranked, std::size_t cap) {
		for (auto const * candidate : ranked)
		{
			if (cap == 0 || result.size () >= limits.max_reps)
			{
				break;
			}
			result.push_back (candidate->account);
			--cap;
		}
	};
	take (observed, limits.observed);
	take (unobserved, limits.explore);

	for (auto const & account : result)
	{
		last_asked[account] = now;
	}
	return result;
}

/*
 * vote_solicitor_round
 */

nano::vote_solicitor_round::vote_solicitor_round (std::deque<nano::representative> const & reps, nano::solicitation_constants const & limits_a, uint64_t seed, nano::relay_targets relay_targets_a) :
	limits{ limits_a },
	relay_reps_m{ std::move (relay_targets_a.reps) },
	relays_m{ std::move (relay_targets_a.relays) },
	rng_m{ seed }
{
	debug_assert (std::none_of (reps.begin (), reps.end (), [] (auto const & rep) { return rep.channel == nullptr; }));

	// Reps come heaviest first, so each channel ranks by its heaviest rep
	boost::unordered_flat_map<std::shared_ptr<nano::transport::channel>, std::size_t> index;
	index.reserve (reps.size ());
	for (auto const & rep : reps)
	{
		if (auto existing = index.find (rep.channel); existing != index.end ())
		{
			channels_m[existing->second].reps.push_back (rep.account);
			continue;
		}
		// Each channel is asked for room once, a channel already at max gets nothing this round
		auto const requests_full = rep.channel->max (nano::transport::traffic_type::confirmation_requests);
		auto const broadcasts_full = rep.channel->max (nano::transport::traffic_type::block_broadcast);
		counts_m.full_channels += (requests_full ? 1 : 0) + (broadcasts_full ? 1 : 0);
		index.emplace (rep.channel, channels_m.size ());
		channels_m.push_back ({ rep.channel, { rep.account }, { requests_full ? 0 : limits.channel_messages, broadcasts_full ? 0 : limits.channel_messages } });
	}
}

template <typename Decide>
auto nano::vote_solicitor_round::decide_channel (nano::election_snapshot const & snapshot, channel_entry const & entry, Decide const & decide) -> rep_decision
{
	auto result = rep_decision::skip;
	for (auto const & rep : entry.reps)
	{
		result = std::max (result, decide (snapshot, rep));
	}
	return result;
}

auto nano::vote_solicitor_round::decide_vote_request (nano::election_snapshot const & snapshot, nano::account const & rep) -> rep_decision
{
	auto const existing = snapshot.votes.find (rep);
	if (existing == snapshot.votes.end ())
	{
		return rep_decision::solicit;
	}
	if (existing->second.hash != snapshot.winner->hash ())
	{
		// A final vote never changes, asking again would only repeat it
		return existing->second.final () ? rep_decision::skip : rep_decision::solicit_fork;
	}
	// Before quorum any vote for the winner is conclusive, after quorum only a final one is
	if (snapshot.quorum && !existing->second.final ())
	{
		return rep_decision::solicit;
	}
	return rep_decision::skip;
}

auto nano::vote_solicitor_round::decide_block_broadcast (nano::election_snapshot const & snapshot, nano::account const & rep) -> rep_decision
{
	auto const existing = snapshot.votes.find (rep);
	if (existing == snapshot.votes.end ())
	{
		return rep_decision::solicit;
	}
	if (existing->second.hash != snapshot.winner->hash ())
	{
		// A final vote never changes, the winner block cannot win the rep over
		return existing->second.final () ? rep_decision::skip : rep_decision::solicit_fork;
	}
	return rep_decision::skip;
}

bool nano::vote_solicitor_round::request_votes (nano::election_snapshot const & snapshot)
{
	debug_assert (snapshot.winner != nullptr);

	auto const hash = snapshot.winner->hash ();
	auto const root = snapshot.qualified_root.root ();

	bool planned = false;
	std::size_t count = 0; // Channels without a vote asked so far
	std::size_t fork_count = 0; // Channels with a fork voter asked so far
	for (auto const & entry : channels_m)
	{
		auto const decision = decide_channel (snapshot, entry, decide_vote_request);
		if (decision == rep_decision::skip)
		{
			continue;
		}
		// Each kind of channel fills its own cap heaviest first, fork voters never take slots from the others
		auto const & filled = decision == rep_decision::solicit_fork ? fork_count : count;
		auto const cap = decision == rep_decision::solicit_fork ? limits.election_fork_requests : limits.election_vote_requests;
		if (filled >= cap)
		{
			continue;
		}
		// The budgeted messages hold as many hashes as a confirm_req carries
		auto const existing = vote_requests_m.find (entry.channel);
		auto const pending = existing != vote_requests_m.end () ? existing->second.size () : 0;
		auto const capacity = entry.budget.requests * nano::messages::confirm_req::max_hashes;
		if (pending >= capacity)
		{
			++counts_m.exhausted;
			continue;
		}
		vote_requests_m[entry.channel].emplace_back (hash, root);
		++(decision == rep_decision::solicit_fork ? fork_count : count);
		planned = true;
	}
	counts_m.requested_elections += planned ? 1 : 0;
	return planned;
}

bool nano::vote_solicitor_round::broadcast_block (nano::election_snapshot const & snapshot)
{
	debug_assert (snapshot.winner != nullptr);

	// Split the round's channels by what this election needs from them
	std::deque<channel_ref> forks; // A rep behind the channel voted for a different hash, competing for the fork cap
	std::deque<channel_ref> missing; // A rep behind the channel has not voted yet, competing for the other cap
	bool blocked = false; // Some channel needed the block but had no budget left this round

	for (auto & entry : channels_m)
	{
		auto const decision = decide_channel (snapshot, entry, decide_block_broadcast);
		if (decision == rep_decision::skip)
		{
			continue; // Every rep behind the channel already voted for the winner
		}
		if (entry.budget.broadcasts == 0)
		{
			blocked = true;
			++counts_m.exhausted;
			continue;
		}
		(decision == rep_decision::solicit_fork ? forks : missing).push_back (entry);
	}

	// A uniform random sample of each group up to its cap, so no rep draws every node's copy
	std::deque<channel_ref> chosen;
	std::sample (forks.begin (), forks.end (), std::back_inserter (chosen), limits.election_fork_broadcasts, rng_m);
	std::sample (missing.begin (), missing.end (), std::back_inserter (chosen), limits.election_block_broadcasts, rng_m);

	channels_t targets;
	for (channel_entry & entry : chosen)
	{
		--entry.budget.broadcasts;
		targets.push_back (entry.channel);
	}

	// Only spent budgets kept the block from reps that need it, the election stays due for a later round
	if (targets.empty () && blocked)
	{
		return false;
	}

	// No rep needs the block, the empty broadcast counts as done so the election is not planned again every round
	if (targets.empty ())
	{
		++counts_m.broadcasts_without_targets;
	}

	block_broadcasts_m.push_back ({ snapshot.winner, std::move (targets) });
	return true;
}

bool nano::vote_solicitor_round::relay_request (nano::election_snapshot const & snapshot)
{
	debug_assert (snapshot.winner != nullptr);

	if (relays_m.empty () || relay_reps_m.empty ())
	{
		return false;
	}

	// Filtering keeps the selection order, so elections missing the same reps produce the same key
	std::deque<nano::account> reps;
	for (auto const & rep : relay_reps_m)
	{
		if (decide_vote_request (snapshot, rep) != rep_decision::skip)
		{
			reps.push_back (rep);
		}
	}
	if (reps.empty ())
	{
		return false;
	}

	// Before quorum any vote settles a rep, after it only a final one does
	auto const finality = snapshot.quorum ? nano::vote_finality::final : nano::vote_finality::any;
	relay_requests_m[{ std::move (reps), finality }].emplace_back (snapshot.winner->hash (), snapshot.qualified_root.root ());
	return true;
}

auto nano::vote_solicitor_round::vote_requests () const -> vote_requests_t const &
{
	return vote_requests_m;
}

auto nano::vote_solicitor_round::block_broadcasts () const -> block_broadcasts_t const &
{
	return block_broadcasts_m;
}

auto nano::vote_solicitor_round::relay_requests () const -> relay_requests_t
{
	relay_requests_t result;
	for (auto const & [key, roots_hashes] : relay_requests_m)
	{
		result.push_back ({ key.first, key.second, roots_hashes });
	}
	return result;
}

std::deque<nano::account> const & nano::vote_solicitor_round::relay_reps () const
{
	return relay_reps_m;
}

auto nano::vote_solicitor_round::relays () const -> channels_t const &
{
	return relays_m;
}

auto nano::vote_solicitor_round::counts () const -> planning_counts const &
{
	return counts_m;
}

/*
 * vote_solicitor
 */

nano::vote_solicitor::vote_solicitor (nano::vote_solicitor_config const & config_a, nano::rep_crawler & rep_crawler_a, nano::rep_tiers & rep_tiers_a, nano::online_reps & online_reps_a, nano::ledger & ledger_a, nano::vote_relay_client & vote_relay_client_a, nano::network_params const & network_params_a, nano::stats & stats_a, nano::logger & logger_a) :
	config{ config_a },
	rep_crawler{ rep_crawler_a },
	rep_tiers{ rep_tiers_a },
	online_reps{ online_reps_a },
	ledger{ ledger_a },
	vote_relay_client{ vote_relay_client_a },
	network_params{ network_params_a },
	stats{ stats_a },
	logger{ logger_a },
	selector{ { .max_reps = config_a.relay_reps, .observed = config_a.relay_reps_observed, .explore = config_a.relay_reps_explore } }
{
}

nano::vote_solicitor_round nano::vote_solicitor::prepare ()
{
	stats.inc (nano::stat::type::vote_solicitor, nano::stat::detail::loop);

	auto direct = rep_crawler.principal_representatives ();

	// Relays are only worth asking for principal reps this node cannot reach itself
	nano::relay_targets targets;
	if (config.relay_fanout > 0)
	{
		targets.relays = vote_relay_client.relays (config.relay_fanout);
		if (!targets.relays.empty ())
		{
			auto const candidates = relay_candidates (direct);

			std::lock_guard guard{ mutex };
			targets.reps = selector.select (candidates, std::chrono::steady_clock::now ());
		}
	}

	return { direct, network_params.solicitation, random.random (std::numeric_limits<uint64_t>::max ()), std::move (targets) };
}

std::deque<nano::relay_rep_selector::candidate> nano::vote_solicitor::relay_candidates (std::deque<nano::representative> const & direct) const
{
	std::unordered_set<nano::account> reachable;
	for (auto const & rep : direct)
	{
		reachable.insert (rep.account);
	}

	auto const online = online_reps.list ();
	std::unordered_set<nano::account> const observed{ online.begin (), online.end () };

	std::deque<nano::relay_rep_selector::candidate> result;
	for (auto const & rep : rep_tiers.principal_representatives ())
	{
		if (reachable.contains (rep))
		{
			continue;
		}
		result.push_back ({ rep, ledger.weight (rep), observed.contains (rep) });
	}
	return result;
}

nano::solicitation_result nano::vote_solicitor::flush (nano::vote_solicitor_round const & round)
{
	nano::solicitation_result result;
	result.block_broadcasts.reserve (round.block_broadcasts ().size ());
	result.vote_requests.reserve (round.counts ().requested_elections);

	for (auto const & broadcast : round.block_broadcasts ())
	{
		// No channel at all means no rep needed the block, there was nothing to send
		bool sent = broadcast.channels.empty ();
		nano::messages::publish message{ network_params.network, broadcast.block };
		for (auto const & channel : broadcast.channels)
		{
			if (channel->send (message, nano::transport::traffic_type::block_broadcast))
			{
				stats.inc (nano::stat::type::vote_solicitor, nano::stat::detail::broadcast, nano::stat::dir::out);
				sent = true;
			}
			else
			{
				stats.inc (nano::stat::type::vote_solicitor, nano::stat::detail::broadcast_failed, nano::stat::dir::out);
				logger.debug (nano::log::type::vote_solicitor, "Channel refused winner block: {} to: {}", broadcast.block->hash (), channel);
			}
		}
		if (sent)
		{
			result.block_broadcasts.insert (broadcast.block->hash ());
		}
	}

	for (auto const & [channel, roots_hashes] : round.vote_requests ())
	{
		for (auto const & batch : nano::messages::confirm_req::batch_hashes (roots_hashes))
		{
			nano::messages::confirm_req message{ network_params.network, batch };
			if (channel->send (message, nano::transport::traffic_type::confirmation_requests))
			{
				stats.inc (nano::stat::type::vote_solicitor, nano::stat::detail::request, nano::stat::dir::out);
				stats.add (nano::stat::type::vote_solicitor, nano::stat::detail::request_hashes, nano::stat::dir::out, batch.size ());
				for (auto const & [hash, root] : batch)
				{
					result.vote_requests.insert (hash);
				}
			}
			else
			{
				stats.inc (nano::stat::type::vote_solicitor, nano::stat::detail::request_failed, nano::stat::dir::out);
				logger.debug (nano::log::type::vote_solicitor, "Channel refused vote request for {} hashes: {}", batch.size (), channel);
			}
		}
	}

	// Spread the requests over the round's relays, a chunk the relay client refuses leaves its elections due
	auto const & relays = round.relays ();
	std::size_t next_relay = 0;
	for (auto const & batch : round.relay_requests ())
	{
		debug_assert (!relays.empty ());
		for (std::size_t offset = 0; offset < batch.roots_hashes.size (); offset += nano::messages::vote_relay_req::max_hashes)
		{
			auto const end = std::min (offset + nano::messages::vote_relay_req::max_hashes, batch.roots_hashes.size ());
			nano::vote_relay_client::roots_hashes_t const chunk (batch.roots_hashes.begin () + offset, batch.roots_hashes.begin () + end);

			auto const & relay = relays[next_relay++ % relays.size ()];
			if (vote_relay_client.request (relay, batch.reps, chunk, batch.finality))
			{
				stats.inc (nano::stat::type::vote_solicitor, nano::stat::detail::relay_request, nano::stat::dir::out);
				for (auto const & [hash, root] : chunk)
				{
					result.relay_requests.insert (hash);
				}
			}
		}
	}

	auto const & counts = round.counts ();
	stats.add (nano::stat::type::vote_solicitor, nano::stat::detail::channel_full, nano::stat::dir::out, counts.full_channels);
	stats.add (nano::stat::type::vote_solicitor, nano::stat::detail::exhausted, nano::stat::dir::out, counts.exhausted);
	stats.add (nano::stat::type::vote_solicitor, nano::stat::detail::no_targets, nano::stat::dir::out, counts.broadcasts_without_targets);

	return result;
}

/*
 * vote_solicitor_config
 */

nano::error nano::vote_solicitor_config::serialize (nano::tomlconfig & toml) const
{
	toml.put ("relay_fanout", relay_fanout, "Number of relay peers asked per election loop round, 0 disables relay requests. \ntype:uint64");
	toml.put ("relay_reps", relay_reps, "Maximum number of representatives per relay request. \ntype:uint64");
	toml.put ("relay_reps_observed", relay_reps_observed, "Relay request slots for representatives seen voting recently, heaviest first. \ntype:uint64");
	toml.put ("relay_reps_explore", relay_reps_explore, "Relay request slots for representatives not seen voting, rotated so every one gets asked. \ntype:uint64");

	return toml.get_error ();
}

nano::error nano::vote_solicitor_config::deserialize (nano::tomlconfig & toml)
{
	toml.get ("relay_fanout", relay_fanout);
	toml.get ("relay_reps", relay_reps);
	toml.get ("relay_reps_observed", relay_reps_observed);
	toml.get ("relay_reps_explore", relay_reps_explore);

	if (relay_reps > nano::messages::vote_relay_req::max_reps)
	{
		toml.get_error ().set ("relay_reps must not exceed " + std::to_string (nano::messages::vote_relay_req::max_reps));
	}
	if (relay_reps_observed + relay_reps_explore > relay_reps)
	{
		toml.get_error ().set ("relay_reps_observed and relay_reps_explore together must not exceed relay_reps");
	}

	return toml.get_error ();
}
