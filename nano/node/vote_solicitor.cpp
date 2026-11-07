#include <nano/lib/blocks.hpp>
#include <nano/lib/stats.hpp>
#include <nano/messages/confirm.hpp>
#include <nano/messages/publish.hpp>
#include <nano/node/network.hpp>
#include <nano/node/repcrawler.hpp>
#include <nano/node/transport/channel.hpp>
#include <nano/node/transport/traffic_type.hpp>
#include <nano/node/vote_solicitor.hpp>

#include <algorithm>

/*
 * vote_solicitor_round
 */

nano::vote_solicitor_round::vote_solicitor_round (std::deque<nano::representative> reps, nano::solicitation_limits const & limits_a) :
	limits{ limits_a },
	vote_request_reps{ reps },
	block_broadcast_reps{ std::move (reps) }
{
	debug_assert (std::none_of (block_broadcast_reps.begin (), block_broadcast_reps.end (), [] (auto const & rep) { return rep.channel == nullptr; }));
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
		if (it->channel->max (nano::transport::traffic_type::confirmation_requests))
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

auto nano::vote_solicitor_round::vote_requests () const -> boost::unordered_flat_map<std::shared_ptr<nano::transport::channel>, roots_hashes_t> const &
{
	return vote_requests_m;
}

auto nano::vote_solicitor_round::block_broadcasts () const -> std::deque<std::pair<std::shared_ptr<nano::block>, channels_t>> const &
{
	return block_broadcasts_m;
}

/*
 * vote_solicitor
 */

nano::vote_solicitor::vote_solicitor (nano::network & network_a, nano::rep_crawler & rep_crawler_a, nano::network_constants const & network_constants_a, nano::stats & stats_a) :
	network{ network_a },
	rep_crawler{ rep_crawler_a },
	network_constants{ network_constants_a },
	stats{ stats_a }
{
}

nano::vote_solicitor_round nano::vote_solicitor::prepare ()
{
	return { rep_crawler.principal_representatives (), round_limits () };
}

nano::solicitation_limits nano::vote_solicitor::round_limits () const
{
	return {
		.election_vote_requests = 50,
		.election_block_broadcasts = std::max<std::size_t> (network.fanout () / 2, 1),
		.round_block_broadcasts = network_constants.is_dev_network () ? 4u : 30u,
	};
}

void nano::vote_solicitor::flush (nano::vote_solicitor_round const & round)
{
	for (auto const & [block, channels] : round.block_broadcasts ())
	{
		nano::messages::publish message{ network_constants, block };
		for (auto const & channel : channels)
		{
			if (channel->send (message, nano::transport::traffic_type::block_broadcast))
			{
				stats.inc (nano::stat::type::vote_solicitor, nano::stat::detail::broadcast, nano::stat::dir::out);
			}
		}
	}

	for (auto const & [channel, roots_hashes] : round.vote_requests ())
	{
		for (auto const & batch : nano::messages::confirm_req::batch_hashes (roots_hashes))
		{
			nano::messages::confirm_req message{ network_constants, batch };
			if (channel->send (message, nano::transport::traffic_type::confirmation_requests))
			{
				stats.inc (nano::stat::type::vote_solicitor, nano::stat::detail::request, nano::stat::dir::out);
			}
		}
	}
}
