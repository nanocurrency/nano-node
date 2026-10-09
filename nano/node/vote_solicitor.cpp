#include <nano/lib/blocks.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/tomlconfig.hpp>
#include <nano/messages/confirm.hpp>
#include <nano/messages/publish.hpp>
#include <nano/messages/vote_relay.hpp>
#include <nano/node/network.hpp>
#include <nano/node/online_reps.hpp>
#include <nano/node/rep_tiers.hpp>
#include <nano/node/repcrawler.hpp>
#include <nano/node/transport/channel.hpp>
#include <nano/node/transport/traffic_type.hpp>
#include <nano/node/vote_relay_client.hpp>
#include <nano/node/vote_solicitor.hpp>
#include <nano/secure/ledger.hpp>

#include <algorithm>
#include <deque>
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

nano::vote_solicitor_round::vote_solicitor_round (nano::solicitation_targets targets, nano::solicitation_limits const & limits_a) :
	limits{ limits_a },
	vote_request_reps{ targets.direct },
	block_broadcast_reps{ std::move (targets.direct) },
	relay_reps_m{ std::move (targets.relayed) },
	relays_m{ std::move (targets.relays) }
{
}

bool nano::vote_solicitor_round::default_channel_full (std::shared_ptr<nano::transport::channel> const & channel)
{
	return channel != nullptr && channel->max (nano::transport::traffic_type::confirmation_requests);
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
		return rep_decision::solicit_fork;
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
		return rep_decision::solicit_fork;
	}
	return rep_decision::skip;
}

bool nano::vote_solicitor_round::request_votes (nano::election_snapshot const & snapshot)
{
	debug_assert (snapshot.winner != nullptr);

	auto const hash = snapshot.winner->hash ();
	auto const root = snapshot.qualified_root.root ();

	bool planned = false;
	std::size_t count = 0;
	for (auto it = vote_request_reps.begin (); it != vote_request_reps.end () && count < limits.election_vote_requests;)
	{
		auto const decision = decide_vote_request (snapshot, it->account);
		if (decision == rep_decision::skip)
		{
			++it;
			continue;
		}
		if (channel_full (it->channel))
		{
			it = vote_request_reps.erase (it); // Leave the channel alone for the rest of the round
			continue;
		}
		vote_requests_m[it->channel].emplace_back (hash, root);
		count += (decision == rep_decision::solicit_fork) ? 0 : 1;
		planned = true;
		++it;
	}
	return planned;
}

bool nano::vote_solicitor_round::broadcast_block (nano::election_snapshot const & snapshot)
{
	debug_assert (snapshot.winner != nullptr);

	if (block_broadcast_count >= limits.round_block_broadcasts)
	{
		return false;
	}
	++block_broadcast_count;

	channels_t targets;
	std::size_t count = 0;
	for (auto const & rep : block_broadcast_reps)
	{
		if (count >= limits.election_block_broadcasts)
		{
			break;
		}
		auto const decision = decide_block_broadcast (snapshot, rep.account);
		if (decision == rep_decision::skip)
		{
			continue;
		}
		targets.push_back (rep.channel);
		count += (decision == rep_decision::solicit_fork) ? 0 : 1;
	}

	block_broadcasts_m.emplace_back (snapshot.winner, std::move (targets));
	return true;
}

bool nano::vote_solicitor_round::relay_request (nano::election_snapshot const & snapshot)
{
	debug_assert (snapshot.winner != nullptr);

	if (relays_m.empty () || relay_reps_m.empty ())
	{
		return false;
	}

	// Filtering keeps the window order, so elections missing the same reps produce the same key
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

	bool const include_non_final = !snapshot.quorum;
	relay_requests_m[{ std::move (reps), include_non_final }].emplace_back (snapshot.winner->hash (), snapshot.qualified_root.root ());
	return true;
}

auto nano::vote_solicitor_round::vote_requests () const -> std::unordered_map<std::shared_ptr<nano::transport::channel>, roots_hashes_t> const &
{
	return vote_requests_m;
}

auto nano::vote_solicitor_round::block_broadcasts () const -> std::vector<std::pair<std::shared_ptr<nano::block>, channels_t>> const &
{
	return block_broadcasts_m;
}

auto nano::vote_solicitor_round::relay_requests () const -> std::deque<relay_batch>
{
	std::deque<relay_batch> result;
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

/*
 * vote_solicitor
 */

nano::vote_solicitor::vote_solicitor (vote_solicitor_config const & config_a, nano::network & network_a, nano::rep_crawler & rep_crawler_a, nano::rep_tiers & rep_tiers_a, nano::online_reps & online_reps_a, nano::ledger & ledger_a, nano::vote_relay_client & vote_relay_client_a, nano::network_constants const & network_constants_a, nano::stats & stats_a, nano::logger & logger_a) :
	limits{
		.election_vote_requests = 50,
		.election_block_broadcasts = 0, // Derived from the network fanout at each prepare
		.round_block_broadcasts = network_constants_a.is_dev_network () ? 4u : 30u,
	},
	config{ config_a },
	network{ network_a },
	rep_crawler{ rep_crawler_a },
	rep_tiers{ rep_tiers_a },
	online_reps{ online_reps_a },
	ledger{ ledger_a },
	vote_relay_client{ vote_relay_client_a },
	network_constants{ network_constants_a },
	stats{ stats_a },
	logger{ logger_a },
	selector{ { .max_reps = config_a.relay_reps, .observed = config_a.relay_reps_observed, .explore = config_a.relay_reps_explore } }
{
}

nano::vote_solicitor_round nano::vote_solicitor::prepare ()
{
	nano::solicitation_targets targets{
		.direct = rep_crawler.principal_representatives (),
	};

	// Relays are only worth asking for principal reps this node cannot reach itself
	if (config.relay_fanout > 0)
	{
		targets.relays = vote_relay_client.relays (config.relay_fanout);
		if (!targets.relays.empty ())
		{
			auto const candidates = relay_candidates (targets.direct);

			nano::lock_guard<nano::mutex> guard{ mutex };
			targets.relayed = selector.select (candidates, std::chrono::steady_clock::now ());
		}
	}

	auto round_limits = limits;
	round_limits.election_block_broadcasts = std::max<std::size_t> (network.fanout () / 2, 1);

	return { std::move (targets), round_limits };
}

std::deque<nano::relay_rep_selector::candidate> nano::vote_solicitor::relay_candidates (std::vector<nano::representative> const & direct) const
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

void nano::vote_solicitor::flush (nano::vote_solicitor_round const & round)
{
	for (auto const & [block, channels] : round.block_broadcasts ())
	{
		nano::messages::publish message{ network_constants, block };
		for (auto const & channel : channels)
		{
			stats.inc (nano::stat::type::vote_solicitor, nano::stat::detail::broadcast, nano::stat::dir::out);
			channel->send (message, nano::transport::traffic_type::block_broadcast);
		}
	}

	for (auto const & [channel, roots_hashes] : round.vote_requests ())
	{
		for (std::size_t offset = 0; offset < roots_hashes.size (); offset += nano::network::confirm_req_hashes_max)
		{
			auto const end = std::min (offset + nano::network::confirm_req_hashes_max, roots_hashes.size ());
			std::vector<std::pair<nano::block_hash, nano::root>> const batch (roots_hashes.begin () + offset, roots_hashes.begin () + end);

			stats.inc (nano::stat::type::vote_solicitor, nano::stat::detail::request, nano::stat::dir::out);
			nano::messages::confirm_req message{ network_constants, batch };
			channel->send (message, nano::transport::traffic_type::confirmation_requests);
		}
	}

	auto const & relays = round.relays ();
	if (relays.empty ())
	{
		return;
	}
	std::size_t next_relay = 0;
	for (auto const & batch : round.relay_requests ())
	{
		for (std::size_t offset = 0; offset < batch.roots_hashes.size (); offset += nano::messages::vote_relay_req::max_hashes)
		{
			auto const end = std::min (offset + nano::messages::vote_relay_req::max_hashes, batch.roots_hashes.size ());
			nano::vote_relay_client::roots_hashes_t const chunk (batch.roots_hashes.begin () + offset, batch.roots_hashes.begin () + end);

			// Spread the requests over the round's relays
			auto const & relay = relays[next_relay++ % relays.size ()];
			if (vote_relay_client.request (relay, batch.reps, chunk, batch.include_non_final))
			{
				stats.inc (nano::stat::type::vote_solicitor, nano::stat::detail::relay_request, nano::stat::dir::out);
			}
		}
	}
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
