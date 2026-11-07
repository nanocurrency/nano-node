#include <nano/lib/blocks.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/stats.hpp>
#include <nano/messages/confirm.hpp>
#include <nano/messages/publish.hpp>
#include <nano/node/election.hpp>
#include <nano/node/network.hpp>
#include <nano/node/repcrawler.hpp>
#include <nano/node/transport/channel.hpp>
#include <nano/node/transport/formatting.hpp>
#include <nano/node/transport/traffic_type.hpp>
#include <nano/node/vote_solicitor.hpp>

#include <algorithm>

/*
 * vote_solicitor_round
 */

nano::vote_solicitor_round::vote_solicitor_round (std::deque<nano::representative> reps_a, nano::solicitation_limits const & limits_a) :
	limits{ limits_a },
	reps{ std::move (reps_a) }
{
	debug_assert (std::none_of (reps.begin (), reps.end (), [] (auto const & rep) { return rep.channel == nullptr; }));
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
	for (auto const & rep : reps)
	{
		if (count >= limits.election_vote_requests)
		{
			break;
		}
		auto const decision = decide_vote_request (snapshot, rep.account);
		if (decision == rep_decision::skip || full_channels.contains (rep.channel))
		{
			continue;
		}
		if (rep.channel->max (nano::transport::traffic_type::confirmation_requests))
		{
			full_channels.insert (rep.channel);
			++counts_m.full_channels;
			continue;
		}
		vote_requests_m[rep.channel].emplace_back (hash, root);
		count += (decision == rep_decision::solicit_fork) ? 0 : 1;
		planned = true;
	}
	counts_m.requested_elections += planned ? 1 : 0;
	return planned;
}

bool nano::vote_solicitor_round::broadcast_block (nano::election_snapshot const & snapshot)
{
	debug_assert (snapshot.winner != nullptr);

	if (block_broadcast_count >= limits.round_block_broadcasts)
	{
		++counts_m.broadcast_cap_hits;
		return false;
	}
	++block_broadcast_count;

	channels_t targets;
	std::size_t count = 0;
	for (auto const & rep : reps)
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

	counts_m.broadcasts_without_targets += targets.empty () ? 1 : 0;
	block_broadcasts_m.push_back ({ snapshot.winner, std::move (targets) });
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

auto nano::vote_solicitor_round::counts () const -> planning_counts const &
{
	return counts_m;
}

/*
 * vote_solicitor
 */

nano::vote_solicitor::vote_solicitor (nano::network & network_a, nano::rep_crawler & rep_crawler_a, nano::network_constants const & network_constants_a, nano::stats & stats_a, nano::logger & logger_a) :
	network{ network_a },
	rep_crawler{ rep_crawler_a },
	network_constants{ network_constants_a },
	stats{ stats_a },
	logger{ logger_a }
{
}

nano::vote_solicitor_round nano::vote_solicitor::prepare ()
{
	stats.inc (nano::stat::type::vote_solicitor, nano::stat::detail::loop);
	return { rep_crawler.principal_representatives (), round_limits () };
}

nano::solicitation_limits nano::vote_solicitor::round_limits () const
{
	nano::solicitation_limits limits;
	limits.election_block_broadcasts = std::max<std::size_t> (network.fanout () / 2, 1);
	if (network_constants.is_dev_network ())
	{
		limits.round_block_broadcasts = 4;
	}
	return limits;
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
		nano::messages::publish message{ network_constants, broadcast.block };
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
			nano::messages::confirm_req message{ network_constants, batch };
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

	auto const & counts = round.counts ();
	stats.add (nano::stat::type::vote_solicitor, nano::stat::detail::channel_full, nano::stat::dir::out, counts.full_channels);
	stats.add (nano::stat::type::vote_solicitor, nano::stat::detail::no_targets, nano::stat::dir::out, counts.broadcasts_without_targets);
	stats.add (nano::stat::type::vote_solicitor, nano::stat::detail::overfill, nano::stat::dir::out, counts.broadcast_cap_hits);

	return result;
}
