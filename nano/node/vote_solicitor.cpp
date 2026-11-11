#include <nano/lib/blocks.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/stats.hpp>
#include <nano/messages/confirm.hpp>
#include <nano/messages/publish.hpp>
#include <nano/node/election.hpp>
#include <nano/node/repcrawler.hpp>
#include <nano/node/transport/channel.hpp>
#include <nano/node/transport/formatting.hpp>
#include <nano/node/transport/traffic_type.hpp>
#include <nano/node/vote_solicitor.hpp>

#include <algorithm>

/*
 * vote_solicitor_round
 */

nano::vote_solicitor_round::vote_solicitor_round (std::deque<nano::representative> const & reps, nano::solicitation_constants const & limits_a) :
	limits{ limits_a }
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
	for (auto const & entry : channels_m)
	{
		auto const decision = decide_channel (snapshot, entry, decide_vote_request);
		if (decision == rep_decision::skip)
		{
			continue;
		}
		// The cap only bounds ordinary targets, a channel with a fork voter behind it is reached wherever it ranks
		if (decision == rep_decision::solicit && count >= limits.election_vote_requests)
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
		count += (decision == rep_decision::solicit_fork) ? 0 : 1;
		planned = true;
	}
	counts_m.requested_elections += planned ? 1 : 0;
	return planned;
}

bool nano::vote_solicitor_round::broadcast_block (nano::election_snapshot const & snapshot)
{
	debug_assert (snapshot.winner != nullptr);

	channels_t targets;
	bool blocked = false; // A rep needed the block but its channel had no budget left
	std::size_t count = 0;
	for (auto & entry : channels_m)
	{
		auto const decision = decide_channel (snapshot, entry, decide_block_broadcast);
		if (decision == rep_decision::skip)
		{
			continue;
		}
		// The cap only bounds ordinary targets, a channel with a fork voter behind it is reached wherever it ranks
		if (decision == rep_decision::solicit && count >= limits.election_block_broadcasts)
		{
			continue;
		}
		if (entry.budget.broadcasts == 0)
		{
			blocked = true;
			++counts_m.exhausted;
			continue;
		}
		--entry.budget.broadcasts;
		targets.push_back (entry.channel);
		count += (decision == rep_decision::solicit_fork) ? 0 : 1;
	}

	// Nothing to send only because the channels had no budget left, the election stays due for a later round
	if (targets.empty () && blocked)
	{
		return false;
	}

	// A broadcast no rep needs is planned without channels, so its election counts as done
	if (targets.empty ())
	{
		++counts_m.broadcasts_without_targets;
	}
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

nano::vote_solicitor::vote_solicitor (nano::rep_crawler & rep_crawler_a, nano::network_params const & network_params_a, nano::stats & stats_a, nano::logger & logger_a) :
	rep_crawler{ rep_crawler_a },
	network_params{ network_params_a },
	stats{ stats_a },
	logger{ logger_a }
{
}

nano::vote_solicitor_round nano::vote_solicitor::prepare ()
{
	stats.inc (nano::stat::type::vote_solicitor, nano::stat::detail::loop);
	return { rep_crawler.principal_representatives (), network_params.solicitation };
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

	auto const & counts = round.counts ();
	stats.add (nano::stat::type::vote_solicitor, nano::stat::detail::channel_full, nano::stat::dir::out, counts.full_channels);
	stats.add (nano::stat::type::vote_solicitor, nano::stat::detail::exhausted, nano::stat::dir::out, counts.exhausted);
	stats.add (nano::stat::type::vote_solicitor, nano::stat::detail::no_targets, nano::stat::dir::out, counts.broadcasts_without_targets);

	return result;
}
