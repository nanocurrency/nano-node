#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/stats.hpp>
#include <nano/messages/confirm.hpp>
#include <nano/messages/publish.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/election.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/repcrawler.hpp>
#include <nano/node/transport/test_channel.hpp>
#include <nano/node/vote_solicitor.hpp>
#include <nano/secure/vote.hpp>
#include <nano/test_common/chains.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/unordered/unordered_flat_set.hpp>

#include <deque>
#include <map>
#include <set>

using namespace std::chrono_literals;

namespace
{
// Winner blocks for the planner only need to be well-formed, they are never validated against a ledger
std::shared_ptr<nano::block> make_winner (uint64_t seed = 100)
{
	nano::block_builder builder;
	return builder
	.send ()
	.previous (nano::dev::genesis->hash ())
	.destination (nano::keypair ().pub)
	.balance (nano::dev::constants.genesis_amount - seed)
	.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
	.work (0)
	.build ();
}

nano::election_snapshot make_snapshot (std::shared_ptr<nano::block> const & winner, nano::election_ballot::vote_map votes = {}, bool quorum = false)
{
	return { winner->qualified_root (), winner, quorum, std::move (votes) };
}

/**
 * Channel that drops every message, what the solicitor planned for it never goes out
 */
class dropping_channel final : public nano::transport::channel
{
public:
	explicit dropping_channel (nano::node & node) :
		channel{ node }
	{
	}

	nano::endpoint get_remote_endpoint () const override
	{
		return {};
	}
	nano::endpoint get_local_endpoint () const override
	{
		return {};
	}
	nano::transport::transport_type get_type () const override
	{
		return nano::transport::transport_type::fake;
	}
	void close () override
	{
	}
	std::string to_string () const override
	{
		return "dropping_channel";
	}

protected:
	bool send_buffer_impl (nano::shared_const_buffer const &, nano::transport::traffic_type, callback_t) override
	{
		return false;
	}
};

nano::vote_info vote_for (nano::block_hash const & hash, bool final = false)
{
	return { std::chrono::steady_clock::now (), final ? nano::vote::timestamp_final : nano::vote::timestamp_min, hash };
}

// Reps with accounts 1 to count, each behind its own channel
std::deque<nano::representative> make_reps (nano::node & node, std::size_t count)
{
	std::deque<nano::representative> reps;
	for (uint64_t i = 1; i <= count; ++i)
	{
		reps.push_back ({ nano::account{ i }, nano::test::fake_channel (node, nano::account{ i }) });
	}
	return reps;
}
}

/*
 * A representative that has not voted should be requested, accumulated per channel with the winner hash and root
 */
TEST (vote_solicitor_round, request_basic)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	nano::vote_solicitor_round round{ make_reps (node, 1), {} };

	auto winner = make_winner ();
	ASSERT_TRUE (round.request_votes (make_snapshot (winner)));

	ASSERT_EQ (1, round.vote_requests ().size ());
	auto const & queued = round.vote_requests ().begin ()->second;
	ASSERT_EQ (1, queued.size ());
	ASSERT_EQ (winner->hash (), queued[0].first);
	ASSERT_EQ (winner->root (), queued[0].second);
	ASSERT_TRUE (round.block_broadcasts ().empty ());
	ASSERT_EQ (1, round.counts ().requested_elections);
}

/*
 * Before quorum any vote for the winner is conclusive, no request is needed
 */
TEST (vote_solicitor_round, skip_voted)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	nano::vote_solicitor_round round{ make_reps (node, 1), {} };

	auto winner = make_winner ();
	ASSERT_FALSE (round.request_votes (make_snapshot (winner, { { nano::account{ 1 }, vote_for (winner->hash ()) } })));
	ASSERT_TRUE (round.vote_requests ().empty ());
	ASSERT_EQ (0, round.counts ().requested_elections);
}

/*
 * After quorum only final votes are conclusive, a non-final voter should be requested again
 */
TEST (vote_solicitor_round, final_upgrade)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	auto winner = make_winner ();
	{
		nano::vote_solicitor_round round{ make_reps (node, 1), {} };
		ASSERT_TRUE (round.request_votes (make_snapshot (winner, { { nano::account{ 1 }, vote_for (winner->hash ()) } }, /* quorum */ true)));
	}
	{
		nano::vote_solicitor_round round{ make_reps (node, 1), {} };
		ASSERT_FALSE (round.request_votes (make_snapshot (winner, { { nano::account{ 1 }, vote_for (winner->hash (), /* final */ true) } }, /* quorum */ true)));
	}
}

/*
 * A vote for a different hash should be re-requested without counting towards the per election cap
 */
TEST (vote_solicitor_round, fork_bypasses_cap)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	std::size_t const max_requests = 3;
	auto reps = make_reps (node, max_requests + 1);
	auto winner = make_winner ();

	// All reps voted for a different hash, the cap does not apply
	{
		nano::vote_solicitor_round round{ reps, { .election_vote_requests = max_requests } };
		nano::election_ballot::vote_map votes;
		for (auto const & rep : reps)
		{
			votes[rep.account] = vote_for (nano::block_hash{ 999 });
		}
		ASSERT_TRUE (round.request_votes (make_snapshot (winner, votes)));
		ASSERT_EQ (max_requests + 1, round.vote_requests ().size ());
	}
	// No votes at all, the cap applies
	{
		nano::vote_solicitor_round round{ reps, { .election_vote_requests = max_requests } };
		ASSERT_TRUE (round.request_votes (make_snapshot (winner)));
		ASSERT_EQ (max_requests, round.vote_requests ().size ());
	}
}

/*
 * A channel at max for requests when the round starts is left alone for the whole round, also once it has room again, because
 * the round asks each channel only once
 */
TEST (vote_solicitor_round, channel_full)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	auto reps = make_reps (node, 2);
	auto const channel_full = reps[0].channel;
	auto const channel_ok = reps[1].channel;
	channel_full->saturate (nano::transport::traffic_type::confirmation_requests);

	nano::vote_solicitor_round round{ reps, {} };
	ASSERT_TRUE (round.request_votes (make_snapshot (make_winner ())));
	ASSERT_EQ (1, round.vote_requests ().size ());
	ASSERT_TRUE (round.vote_requests ().contains (channel_ok));
	ASSERT_FALSE (round.vote_requests ().contains (channel_full));

	// The channel was asked when the round started, having room again changes nothing for this round
	channel_full->unsaturate (nano::transport::traffic_type::confirmation_requests);
	ASSERT_TRUE (round.request_votes (make_snapshot (make_winner (200))));
	ASSERT_FALSE (round.vote_requests ().contains (channel_full));
	ASSERT_EQ (2, round.vote_requests ().at (channel_ok).size ());
	ASSERT_EQ (1, round.counts ().full_channels);
}

/*
 * A rep that voted for the winner is not a broadcast target, a fork voter is targeted without counting towards the cap
 */
TEST (vote_solicitor_round, broadcast_targets)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	auto winner = make_winner ();

	// Voted for the winner, the broadcast still counts as planned but targets nobody
	{
		nano::vote_solicitor_round round{ make_reps (node, 1), {} };
		ASSERT_TRUE (round.broadcast_block (make_snapshot (winner, { { nano::account{ 1 }, vote_for (winner->hash ()) } })));
		ASSERT_EQ (1, round.block_broadcasts ().size ());
		ASSERT_TRUE (round.block_broadcasts ()[0].channels.empty ());
		ASSERT_EQ (1, round.counts ().broadcasts_without_targets);
	}
	// Voted for a different hash, targeted
	{
		nano::vote_solicitor_round round{ make_reps (node, 1), {} };
		ASSERT_TRUE (round.broadcast_block (make_snapshot (winner, { { nano::account{ 1 }, vote_for (nano::block_hash{ 999 }) } })));
		ASSERT_EQ (1, round.block_broadcasts ()[0].channels.size ());
	}
	// The per election cap applies to reps that have not voted, fork voters are exempt
	{
		auto reps = make_reps (node, 4);
		nano::vote_solicitor_round round{ reps, { .election_block_broadcasts = 2 } };
		ASSERT_TRUE (round.broadcast_block (make_snapshot (winner)));
		ASSERT_EQ (2, round.block_broadcasts ()[0].channels.size ());

		nano::election_ballot::vote_map votes;
		for (auto const & rep : reps)
		{
			votes[rep.account] = vote_for (nano::block_hash{ 999 });
		}
		ASSERT_TRUE (round.broadcast_block (make_snapshot (winner, votes)));
		ASSERT_EQ (4, round.block_broadcasts ()[1].channels.size ());
	}
}

/*
 * A round prepared against the reachable representatives should send its requests as batched confirm_req and its broadcasts as publish
 */
TEST (vote_solicitor, flush)
{
	nano::test::system system;
	nano::node_flags flags;
	flags.disable_rep_crawler = true;
	flags.disable_request_loop = true;
	auto & node = *system.add_node (flags);

	auto channel = nano::test::test_channel (node);
	node.rep_crawler.force_add_rep (nano::dev::genesis_key.pub, channel);

	std::atomic<std::size_t> confirm_reqs{ 0 };
	std::atomic<std::size_t> confirm_req_hashes{ 0 };
	std::atomic<std::size_t> publishes{ 0 };
	channel->observe<nano::messages::confirm_req> ([&] (nano::messages::confirm_req const & message) {
		confirm_reqs += 1;
		confirm_req_hashes += message.roots_hashes.size ();
	});
	channel->observe<nano::messages::publish> ([&] (nano::messages::publish const & message) {
		publishes += 1;
	});

	auto round = node.vote_solicitor.prepare ();

	// One election without votes and one where the rep voted for a different hash
	auto winner1 = make_winner (100);
	auto winner2 = make_winner (200);
	ASSERT_TRUE (round.request_votes (make_snapshot (winner1)));
	ASSERT_TRUE (round.broadcast_block (make_snapshot (winner1)));
	ASSERT_TRUE (round.request_votes (make_snapshot (winner2, { { nano::dev::genesis_key.pub, vote_for (nano::block_hash{ 999 }) } })));

	// Nothing is sent before the flush
	ASSERT_EQ (0, confirm_reqs);
	ASSERT_EQ (0, publishes);

	auto const result = node.vote_solicitor.flush (round);

	// Both requests are batched into a single confirm_req to the same channel
	ASSERT_EQ (1, confirm_reqs);
	ASSERT_EQ (2, confirm_req_hashes);
	ASSERT_EQ (1, publishes);

	// The result names the elections each message went out for
	ASSERT_EQ (boost::unordered_flat_set<nano::block_hash>{ winner1->hash () }, result.block_broadcasts);
	ASSERT_EQ ((boost::unordered_flat_set<nano::block_hash>{ winner1->hash (), winner2->hash () }), result.vote_requests);
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_solicitor, nano::stat::detail::request, nano::stat::dir::out));
	ASSERT_EQ (2, node.stats.count (nano::stat::type::vote_solicitor, nano::stat::detail::request_hashes, nano::stat::dir::out));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_solicitor, nano::stat::detail::broadcast, nano::stat::dir::out));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_solicitor, nano::stat::detail::loop));
}

/*
 * Requests to a channel should be split into confirm_req messages at the hash limit
 */
TEST (vote_solicitor, batches)
{
	nano::test::system system;
	nano::node_flags flags;
	flags.disable_rep_crawler = true;
	flags.disable_request_loop = true;
	auto & node = *system.add_node (flags);

	auto channel = nano::test::test_channel (node);
	node.rep_crawler.force_add_rep (nano::dev::genesis_key.pub, channel);

	std::deque<std::size_t> sizes;
	channel->observe<nano::messages::confirm_req> ([&] (nano::messages::confirm_req const & message) {
		sizes.push_back (message.roots_hashes.size ());
	});

	auto round = node.vote_solicitor.prepare ();
	for (std::size_t i = 0; i < nano::messages::confirm_req::max_hashes + 1; ++i)
	{
		ASSERT_TRUE (round.request_votes (make_snapshot (make_winner (100 + i))));
	}
	node.vote_solicitor.flush (round);

	ASSERT_EQ (2, sizes.size ());
	ASSERT_EQ (nano::messages::confirm_req::max_hashes, sizes[0]);
	ASSERT_EQ (1, sizes[1]);
}

/*
 * The election loop should solicit an active election: the representative receives a confirm_req and the winner block
 */
TEST (vote_solicitor, election_round)
{
	nano::test::system system;
	nano::node_flags flags;
	flags.disable_rep_crawler = true;
	auto & node = *system.add_node (flags);

	auto channel = nano::test::test_channel (node);
	node.rep_crawler.force_add_rep (nano::dev::genesis_key.pub, channel);

	std::atomic<std::size_t> confirm_reqs{ 0 };
	std::atomic<std::size_t> publishes{ 0 };
	channel->observe<nano::messages::confirm_req> ([&] (nano::messages::confirm_req const & message) {
		confirm_reqs += 1;
	});
	channel->observe<nano::messages::publish> ([&] (nano::messages::publish const & message) {
		publishes += 1;
	});

	nano::block_builder builder;
	auto send = builder
				.send ()
				.previous (nano::dev::genesis->hash ())
				.destination (nano::keypair ().pub)
				.balance (nano::dev::constants.genesis_amount - 100)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*system.work.generate (nano::dev::genesis->hash ()))
				.build ();
	ASSERT_EQ (nano::block_status::progress, node.process (send));

	auto election = nano::test::start_election (system, node, send->hash ());
	ASSERT_NE (nullptr, election);

	ASSERT_TIMELY (5s, confirm_reqs >= 1);
	ASSERT_TIMELY (5s, publishes >= 1);
	ASSERT_TIMELY (5s, election->confirmation_request_count >= 1);
}

/*
 * A channel that drops the messages leaves nothing in the result, so the elections stay due for a request and a broadcast
 */
TEST (vote_solicitor, flush_dropped)
{
	nano::test::system system;
	nano::node_flags flags;
	flags.disable_rep_crawler = true;
	flags.disable_request_loop = true;
	auto & node = *system.add_node (flags);

	// The only reachable representative sits behind a channel that drops everything
	node.rep_crawler.force_add_rep (nano::dev::genesis_key.pub, std::make_shared<dropping_channel> (node));

	auto round = node.vote_solicitor.prepare ();
	auto winner = make_winner (100);
	ASSERT_TRUE (round.request_votes (make_snapshot (winner)));
	ASSERT_TRUE (round.broadcast_block (make_snapshot (winner)));

	// Nothing went out, nothing is reported or counted
	auto const result = node.vote_solicitor.flush (round);
	ASSERT_TRUE (result.vote_requests.empty ());
	ASSERT_TRUE (result.block_broadcasts.empty ());
	ASSERT_EQ (0, node.stats.count (nano::stat::type::vote_solicitor, nano::stat::detail::request, nano::stat::dir::out));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::vote_solicitor, nano::stat::detail::broadcast, nano::stat::dir::out));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_solicitor, nano::stat::detail::request_failed, nano::stat::dir::out));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_solicitor, nano::stat::detail::broadcast_failed, nano::stat::dir::out));
}

/*
 * A broadcast without targets because every rep already voted for the winner is reported as done, so the election is not planned again every round
 */
TEST (vote_solicitor, flush_all_voted)
{
	nano::test::system system;
	nano::node_flags flags;
	flags.disable_rep_crawler = true;
	flags.disable_request_loop = true;
	auto & node = *system.add_node (flags);

	node.rep_crawler.force_add_rep (nano::dev::genesis_key.pub, std::make_shared<dropping_channel> (node));

	auto round = node.vote_solicitor.prepare ();
	auto winner = make_winner (100);
	ASSERT_TRUE (round.broadcast_block (make_snapshot (winner, { { nano::dev::genesis_key.pub, vote_for (winner->hash ()) } })));
	ASSERT_TRUE (round.block_broadcasts ()[0].channels.empty ());

	auto const result = node.vote_solicitor.flush (round);
	ASSERT_EQ (boost::unordered_flat_set<nano::block_hash>{ winner->hash () }, result.block_broadcasts);
	ASSERT_EQ (0, node.stats.count (nano::stat::type::vote_solicitor, nano::stat::detail::broadcast, nano::stat::dir::out));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_solicitor, nano::stat::detail::no_targets, nano::stat::dir::out));
}

/*
 * A broadcast without targets because no representative is reachable at all is reported as done as well
 */
TEST (vote_solicitor, flush_no_reps)
{
	nano::test::system system;
	nano::node_flags flags;
	flags.disable_rep_crawler = true;
	flags.disable_request_loop = true;
	auto & node = *system.add_node (flags);
	ASSERT_TRUE (node.rep_crawler.principal_representatives ().empty ());

	auto round = node.vote_solicitor.prepare ();
	auto winner = make_winner (100);
	ASSERT_TRUE (round.broadcast_block (make_snapshot (winner)));
	ASSERT_TRUE (round.block_broadcasts ()[0].channels.empty ());

	// Nor is there anyone to ask for votes
	ASSERT_FALSE (round.request_votes (make_snapshot (winner)));

	auto const result = node.vote_solicitor.flush (round);
	ASSERT_EQ (boost::unordered_flat_set<nano::block_hash>{ winner->hash () }, result.block_broadcasts);
	ASSERT_TRUE (result.vote_requests.empty ());
	ASSERT_EQ (0, node.stats.count (nano::stat::type::vote_solicitor, nano::stat::detail::broadcast, nano::stat::dir::out));
}

/*
 * Without any representative to target, the election loop should still stamp the winner broadcast instead of planning it again every round
 */
TEST (vote_solicitor, election_round_no_reps)
{
	nano::test::system system;
	nano::node_flags flags;
	flags.disable_rep_crawler = true;
	auto & node = *system.add_node (flags);
	ASSERT_TRUE (node.rep_crawler.principal_representatives ().empty ());

	nano::block_builder builder;
	auto send = builder
				.send ()
				.previous (nano::dev::genesis->hash ())
				.destination (nano::keypair ().pub)
				.balance (nano::dev::constants.genesis_amount - 100)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*system.work.generate (nano::dev::genesis->hash ()))
				.build ();
	ASSERT_EQ (nano::block_status::progress, node.process (send));

	auto election = nano::test::start_election (system, node, send->hash ());
	ASSERT_NE (nullptr, election);

	// The broadcast had no target, yet it counts as performed and paces the next one
	ASSERT_TIMELY_EQ (5s, 1, node.stats.count (nano::stat::type::election, nano::stat::detail::broadcast_block_initial));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::vote_solicitor, nano::stat::detail::broadcast, nano::stat::dir::out));
}

/*
 * After quorum a rep with a non-final vote for the winner is asked for a final vote, but it already has the block and gets no broadcast
 */
TEST (vote_solicitor_round, quorum_request_not_broadcast)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	nano::vote_solicitor_round round{ make_reps (node, 1), {} };

	auto winner = make_winner ();
	auto const snapshot = make_snapshot (winner, { { nano::account{ 1 }, vote_for (winner->hash ()) } }, /* quorum */ true);
	ASSERT_TRUE (round.request_votes (snapshot));
	ASSERT_TRUE (round.broadcast_block (snapshot));
	ASSERT_TRUE (round.block_broadcasts ()[0].channels.empty ());
}

/*
 * A round without representatives plans no requests, a broadcast is still planned without targets
 */
TEST (vote_solicitor_round, no_reps)
{
	nano::vote_solicitor_round round{ {}, {} };

	auto winner = make_winner ();
	ASSERT_FALSE (round.request_votes (make_snapshot (winner)));
	ASSERT_TRUE (round.vote_requests ().empty ());
	ASSERT_EQ (0, round.counts ().requested_elections);

	ASSERT_TRUE (round.broadcast_block (make_snapshot (winner)));
	ASSERT_TRUE (round.block_broadcasts ()[0].channels.empty ());
}

/*
 * A rep behind a full channel does not take a slot of the per election cap
 */
TEST (vote_solicitor_round, channel_full_outside_cap)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	std::size_t const max_requests = 2;

	auto reps = make_reps (node, max_requests + 1);
	reps[0].channel->saturate (nano::transport::traffic_type::confirmation_requests);

	nano::vote_solicitor_round round{ reps, { .election_vote_requests = max_requests } };
	ASSERT_TRUE (round.request_votes (make_snapshot (make_winner ())));
	ASSERT_EQ (max_requests, round.vote_requests ().size ());
	ASSERT_TRUE (round.vote_requests ().contains (reps[1].channel));
	ASSERT_TRUE (round.vote_requests ().contains (reps[2].channel));
}

/*
 * An election counts as requested when any of its requests went out, a channel refusing its copy does not take that away
 */
TEST (vote_solicitor, flush_partial)
{
	nano::test::system system;
	nano::node_flags flags;
	flags.disable_rep_crawler = true;
	flags.disable_request_loop = true;
	auto & node = *system.add_node (flags);

	nano::keypair rep;
	node.rep_crawler.force_add_rep (nano::dev::genesis_key.pub, std::make_shared<dropping_channel> (node));
	node.rep_crawler.force_add_rep (rep.pub, nano::test::test_channel (node));

	nano::vote_solicitor_round round{ node.rep_crawler.representatives (), {} };
	auto winner = make_winner (100);
	ASSERT_TRUE (round.request_votes (make_snapshot (winner)));
	ASSERT_EQ (2, round.vote_requests ().size ());

	auto const result = node.vote_solicitor.flush (round);
	ASSERT_EQ (boost::unordered_flat_set<nano::block_hash>{ winner->hash () }, result.vote_requests);
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_solicitor, nano::stat::detail::request, nano::stat::dir::out));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::message_drop, nano::stat::detail::confirm_req, nano::stat::dir::out));
}

/*
 * A refused request leaves the election due, the election loop asks again on the next rounds instead of waiting out the request interval
 */
TEST (vote_solicitor, election_round_refused)
{
	nano::test::system system;
	nano::node_flags flags;
	flags.disable_rep_crawler = true;
	auto & node = *system.add_node (flags);

	node.rep_crawler.force_add_rep (nano::dev::genesis_key.pub, std::make_shared<dropping_channel> (node));

	nano::block_builder builder;
	auto send = builder
				.send ()
				.previous (nano::dev::genesis->hash ())
				.destination (nano::keypair ().pub)
				.balance (nano::dev::constants.genesis_amount - 100)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*system.work.generate (nano::dev::genesis->hash ()))
				.build ();
	ASSERT_EQ (nano::block_status::progress, node.process (send));

	auto election = nano::test::start_election (system, node, send->hash ());
	ASSERT_NE (nullptr, election);

	// Every round tries again and nothing is recorded as sent
	ASSERT_TIMELY (5s, node.stats.count (nano::stat::type::message_drop, nano::stat::detail::confirm_req, nano::stat::dir::out) >= 3);
	ASSERT_EQ (0, election->confirmation_request_count);
	ASSERT_EQ (0, node.stats.count (nano::stat::type::election, nano::stat::detail::confirmation_request));
}

/*
 * Reps behind the same channel share one request and one broadcast per election, and take one slot of each per election cap
 */
TEST (vote_solicitor_round, shared_channel)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	// Accounts 1 and 2 are hosted by one peer, account 3 by another
	auto const shared = nano::test::fake_channel (node);
	auto const other = nano::test::fake_channel (node);
	std::deque<nano::representative> reps{ { nano::account{ 1 }, shared }, { nano::account{ 2 }, shared }, { nano::account{ 3 }, other } };

	nano::vote_solicitor_round round{ reps, { .election_vote_requests = 2, .election_block_broadcasts = 2 } };
	auto winner = make_winner ();
	ASSERT_TRUE (round.request_votes (make_snapshot (winner)));
	ASSERT_TRUE (round.broadcast_block (make_snapshot (winner)));

	ASSERT_EQ (1, round.vote_requests ().at (shared).size ());
	ASSERT_EQ (1, round.vote_requests ().at (other).size ());
	auto const & targets = round.block_broadcasts ()[0].channels;
	ASSERT_EQ (2, targets.size ());
	ASSERT_EQ (1, std::count (targets.begin (), targets.end (), shared));
	ASSERT_EQ (1, std::count (targets.begin (), targets.end (), other));

	// A rep that still needs the block brings in the shared channel even when its neighbour already voted
	nano::vote_solicitor_round round2{ reps, {} };
	ASSERT_TRUE (round2.request_votes (make_snapshot (winner, { { nano::account{ 1 }, vote_for (winner->hash ()) } })));
	ASSERT_EQ (1, round2.vote_requests ().at (shared).size ());
}

/*
 * A rep behind a channel at max for broadcasts when the round starts is left out, and a broadcast blocked entirely by such channels is not planned so its election stays due
 */
TEST (vote_solicitor_round, broadcast_channel_full)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	auto reps = make_reps (node, 2);
	reps[0].channel->saturate (nano::transport::traffic_type::block_broadcast);

	// The other rep still gets the block, the full channel takes no slot of the per election cap
	{
		nano::vote_solicitor_round round{ reps, { .election_block_broadcasts = 1 } };
		ASSERT_TRUE (round.broadcast_block (make_snapshot (make_winner ())));
		ASSERT_EQ (1, round.block_broadcasts ()[0].channels.size ());
		ASSERT_EQ (reps[1].channel, round.block_broadcasts ()[0].channels[0]);
		ASSERT_EQ (1, round.counts ().full_channels);

		// Requests are paced separately, the channel still takes them
		ASSERT_TRUE (round.request_votes (make_snapshot (make_winner ())));
		ASSERT_TRUE (round.vote_requests ().contains (reps[0].channel));

		// The full channel stays out of broadcasts for later elections in the round, even once it has room again
		reps[0].channel->unsaturate (nano::transport::traffic_type::block_broadcast);
		ASSERT_TRUE (round.broadcast_block (make_snapshot (make_winner (200))));
		ASSERT_EQ (1, round.block_broadcasts ()[1].channels.size ());
		ASSERT_EQ (reps[1].channel, round.block_broadcasts ()[1].channels[0]);
		ASSERT_EQ (1, round.counts ().full_channels);
		reps[0].channel->saturate (nano::transport::traffic_type::block_broadcast);
	}
	// Every rep that needs the block is behind a full channel
	{
		reps[1].channel->saturate (nano::transport::traffic_type::block_broadcast);
		nano::vote_solicitor_round round{ reps, {} };
		ASSERT_FALSE (round.broadcast_block (make_snapshot (make_winner ())));
		ASSERT_TRUE (round.block_broadcasts ().empty ());
		ASSERT_EQ (0, round.counts ().broadcasts_without_targets);
	}
}

/*
 * The random flood of a winner does not depend on the directed broadcast: with the only rep behind a channel that is full for
 * block broadcasts the round plans nothing and the election stays due, but the loop still queues the block for the random flood
 */
TEST (vote_solicitor, election_round_channel_full)
{
	nano::test::system system;

	// The rebroadcaster queues stale elections on its own, keep that out of the window
	nano::node_config config = system.default_config ();
	config.active_elections->stale_threshold = 60s;
	nano::node_flags flags;
	flags.disable_rep_crawler = true;
	auto & node = *system.add_node (config, flags);

	// The only principal rep sits behind a channel that reports block broadcasts as full
	auto channel = nano::test::test_channel (node);
	channel->saturate (nano::transport::traffic_type::block_broadcast);
	node.rep_crawler.force_add_rep (nano::dev::genesis_key.pub, channel);

	std::atomic<std::size_t> publishes{ 0 };
	channel->observe<nano::messages::publish> ([&] (nano::messages::publish const & message) {
		publishes += 1;
	});

	nano::block_builder builder;
	auto send = builder
				.send ()
				.previous (nano::dev::genesis->hash ())
				.destination (nano::keypair ().pub)
				.balance (nano::dev::constants.genesis_amount - 100)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*system.work.generate (nano::dev::genesis->hash ()))
				.build ();
	ASSERT_EQ (nano::block_status::progress, node.process (send));

	auto election = nano::test::start_election (system, node, send->hash ());
	ASSERT_NE (nullptr, election);

	// Every round leaves the full channel alone
	ASSERT_TIMELY (5s, 1 <= node.stats.count (nano::stat::type::vote_solicitor, nano::stat::detail::channel_full, nano::stat::dir::out));

	// The election start queues the winner once on its own, a second push can only come from the loop
	ASSERT_TIMELY (5s, 2 <= node.stats.count (nano::stat::type::block_rebroadcaster, nano::stat::detail::queued));

	// No directed broadcast went out, so nothing is recorded as sent and the election stays due for one
	ASSERT_EQ (0, publishes);
	ASSERT_EQ (0, node.stats.count (nano::stat::type::election, nano::stat::detail::broadcast_block_initial));
}

/*
 * Rounds take their caps from the network's solicitation constants
 */
TEST (vote_solicitor, limits)
{
	nano::test::system system;
	nano::node_flags flags;
	flags.disable_rep_crawler = true;
	flags.disable_request_loop = true;
	auto & node = *system.add_node (flags);

	auto const & constants = node.network_params.solicitation;
	ASSERT_EQ (1, constants.election_block_broadcasts);
	ASSERT_EQ (32, constants.channel_messages);

	// More principal reps than the per election broadcast cap, and more elections than their channels take in one round
	auto const rep1 = nano::test::setup_rep (system, node, nano::dev::constants.genesis_amount / 4);
	auto const rep2 = nano::test::setup_rep (system, node, nano::dev::constants.genesis_amount / 4);
	node.rep_crawler.force_add_rep (rep1.pub, nano::test::test_channel (node, rep1.pub));
	node.rep_crawler.force_add_rep (rep2.pub, nano::test::test_channel (node, rep2.pub));
	ASSERT_EQ (2, node.rep_crawler.principal_representatives ().size ());

	// Each election reaches one rep, so the two channels together take twice the per channel budget before the round is spent
	auto round = node.vote_solicitor.prepare ();
	for (std::size_t i = 0; i < 2 * constants.channel_messages; ++i)
	{
		ASSERT_TRUE (round.broadcast_block (make_snapshot (make_winner (100 + i))));
		ASSERT_EQ (constants.election_block_broadcasts, round.block_broadcasts ()[i].channels.size ());
	}
	ASSERT_FALSE (round.broadcast_block (make_snapshot (make_winner (999))));
	ASSERT_EQ (2 * constants.channel_messages, round.block_broadcasts ().size ());
}

/*
 * A channel takes at most channel_messages confirm_req per round, each packed with as many hashes as the message carries, and a rep
 * whose channel has no room left is skipped without taking a slot of the per election cap
 */
TEST (vote_solicitor_round, request_budget)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	// One rep with one message of budget, so exactly max_hashes elections fit in the round
	auto reps = make_reps (node, 1);
	nano::vote_solicitor_round round{ reps, { .channel_messages = 1 } };
	for (uint64_t i = 0; i < nano::messages::confirm_req::max_hashes; ++i)
	{
		ASSERT_TRUE (round.request_votes (make_snapshot (make_winner (100 + i))));
	}
	ASSERT_EQ (nano::messages::confirm_req::max_hashes, round.vote_requests ().at (reps[0].channel).size ());
	ASSERT_EQ (0, round.counts ().exhausted);

	// The next election would start a second message, which the budget does not cover
	ASSERT_FALSE (round.request_votes (make_snapshot (make_winner (1000))));
	ASSERT_EQ (nano::messages::confirm_req::max_hashes, round.vote_requests ().at (reps[0].channel).size ());
	ASSERT_EQ (1, round.counts ().exhausted);
}

/*
 * A channel takes at most channel_messages winner blocks per round, a rep whose channel is spent is skipped without taking a slot
 * of the per election cap, and a broadcast blocked entirely by spent channels is not planned so its election stays due
 */
TEST (vote_solicitor_round, broadcast_budget)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	// Two reps on their own channels, one winner block per channel per round, one rep per election
	auto reps = make_reps (node, 2);
	nano::vote_solicitor_round round{ reps, { .election_block_broadcasts = 1, .channel_messages = 1 } };

	// The first election spends the first channel
	ASSERT_TRUE (round.broadcast_block (make_snapshot (make_winner (100))));
	ASSERT_EQ (1, round.block_broadcasts ()[0].channels.size ());
	ASSERT_EQ (reps[0].channel, round.block_broadcasts ()[0].channels[0]);

	// The second election skips the spent channel without using its cap slot and reaches the other rep
	ASSERT_TRUE (round.broadcast_block (make_snapshot (make_winner (200))));
	ASSERT_EQ (1, round.block_broadcasts ()[1].channels.size ());
	ASSERT_EQ (reps[1].channel, round.block_broadcasts ()[1].channels[0]);
	ASSERT_EQ (1, round.counts ().exhausted);

	// With both channels spent the third election is not planned at all
	ASSERT_FALSE (round.broadcast_block (make_snapshot (make_winner (300))));
	ASSERT_EQ (2, round.block_broadcasts ().size ());
	ASSERT_EQ (3, round.counts ().exhausted);
	ASSERT_EQ (0, round.counts ().full_channels);

	// A broadcast no rep needs spends nothing, the budget is still there for a winner that has to reach a rep
	nano::vote_solicitor_round round2{ reps, { .election_block_broadcasts = 1, .channel_messages = 1 } };
	auto voted = make_winner (400);
	ASSERT_TRUE (round2.broadcast_block (make_snapshot (voted, { { nano::account{ 1 }, vote_for (voted->hash ()) }, { nano::account{ 2 }, vote_for (voted->hash ()) } })));
	ASSERT_TRUE (round2.block_broadcasts ()[0].channels.empty ());
	ASSERT_TRUE (round2.broadcast_block (make_snapshot (make_winner (500))));
	ASSERT_EQ (reps[0].channel, round2.block_broadcasts ()[1].channels[0]);
}

/*
 * Reps behind the same channel share its budget: one copy of the winner serves both and spends one message, after which neither
 * rep can be reached this round
 */
TEST (vote_solicitor_round, shared_channel_budget)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	// Accounts 1 and 2 are hosted by one peer that takes one winner block this round
	auto const shared = nano::test::fake_channel (node);
	std::deque<nano::representative> reps{ { nano::account{ 1 }, shared }, { nano::account{ 2 }, shared } };
	nano::vote_solicitor_round round{ reps, { .channel_messages = 1 } };

	// One copy covers both reps, the second rep is not counted against the spent budget
	ASSERT_TRUE (round.broadcast_block (make_snapshot (make_winner (100))));
	ASSERT_EQ (1, round.block_broadcasts ()[0].channels.size ());
	ASSERT_EQ (0, round.counts ().exhausted);

	// The next election finds the channel spent, counted once for both reps
	ASSERT_FALSE (round.broadcast_block (make_snapshot (make_winner (200))));
	ASSERT_EQ (1, round.block_broadcasts ().size ());
	ASSERT_EQ (1, round.counts ().exhausted);
}

/*
 * A fork voter ranked after enough ordinary reps to fill the per election caps still gets the request and the winner block
 */
TEST (vote_solicitor_round, fork_voter_past_cap)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	auto const reps = make_reps (node, 4);
	auto const winner = make_winner ();

	// Reps 1 to 3 have not voted, rep 4 voted for a different hash
	auto const snapshot = make_snapshot (winner, { { nano::account{ 4 }, vote_for (nano::block_hash{ 999 }) } });
	nano::vote_solicitor_round round{ reps, { .election_vote_requests = 2, .election_block_broadcasts = 2 } };
	ASSERT_TRUE (round.request_votes (snapshot));
	ASSERT_TRUE (round.broadcast_block (snapshot));

	// The first two take the cap slots, the third is left out, the fork voter is reached anyway
	ASSERT_EQ (3, round.vote_requests ().size ());
	ASSERT_TRUE (round.vote_requests ().contains (reps[0].channel));
	ASSERT_TRUE (round.vote_requests ().contains (reps[1].channel));
	ASSERT_FALSE (round.vote_requests ().contains (reps[2].channel));
	ASSERT_TRUE (round.vote_requests ().contains (reps[3].channel));
	// Two random missing channels take the broadcast cap, the fork voter gets the block on top
	auto const & targets = round.block_broadcasts ()[0].channels;
	ASSERT_EQ (3, targets.size ());
	ASSERT_EQ (reps[3].channel, targets.front ());
}

/*
 * A fork voter past the cap is still bound by its channel's budget
 */
TEST (vote_solicitor_round, fork_voter_past_cap_full_channel)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	auto const reps = make_reps (node, 2);
	reps[1].channel->saturate (nano::transport::traffic_type::confirmation_requests);
	reps[1].channel->saturate (nano::transport::traffic_type::block_broadcast);
	auto const winner = make_winner ();

	auto const snapshot = make_snapshot (winner, { { nano::account{ 2 }, vote_for (nano::block_hash{ 999 }) } });
	nano::vote_solicitor_round round{ reps, { .election_vote_requests = 1, .election_block_broadcasts = 1 } };
	ASSERT_TRUE (round.request_votes (snapshot));
	ASSERT_TRUE (round.broadcast_block (snapshot));

	ASSERT_EQ (1, round.vote_requests ().size ());
	ASSERT_FALSE (round.vote_requests ().contains (reps[1].channel));
	ASSERT_EQ ((nano::vote_solicitor_round::channels_t{ reps[0].channel }), round.block_broadcasts ()[0].channels);
	ASSERT_EQ (2, round.counts ().exhausted);
}

/*
 * A channel hosting an ordinary rep and a fork voter is messaged once and charged nothing, whichever of the two ranks first
 */
TEST (vote_solicitor_round, shared_channel_fork_voter)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	// Accounts 1 and 2 share a peer, account 2 voted for a different hash, accounts 3 and 4 have peers of their own
	auto const shared = nano::test::fake_channel (node);
	auto const other = nano::test::fake_channel (node);
	auto const last = nano::test::fake_channel (node);
	auto const winner = make_winner ();
	auto const snapshot = make_snapshot (winner, { { nano::account{ 2 }, vote_for (nano::block_hash{ 999 }) } });

	std::deque<std::deque<nano::representative>> orders{
		{ { nano::account{ 1 }, shared }, { nano::account{ 2 }, shared }, { nano::account{ 3 }, other }, { nano::account{ 4 }, last } },
		{ { nano::account{ 2 }, shared }, { nano::account{ 1 }, shared }, { nano::account{ 3 }, other }, { nano::account{ 4 }, last } },
	};
	for (auto const & reps : orders)
	{
		nano::vote_solicitor_round round{ reps, { .election_vote_requests = 1, .election_block_broadcasts = 1 } };
		ASSERT_TRUE (round.request_votes (snapshot));
		ASSERT_TRUE (round.broadcast_block (snapshot));

		// The single cap slot goes to the next ordinary channel, the shared one rides on its fork voter
		ASSERT_EQ (2, round.vote_requests ().size ());
		ASSERT_EQ (1, round.vote_requests ().at (shared).size ());
		ASSERT_EQ (1, round.vote_requests ().at (other).size ());
		ASSERT_FALSE (round.vote_requests ().contains (last));
		// The broadcast slot goes to one of the two ordinary channels at random
		auto const & targets = round.block_broadcasts ()[0].channels;
		ASSERT_EQ (2, targets.size ());
		ASSERT_EQ (shared, targets.front ());
		ASSERT_TRUE (targets.back () == other || targets.back () == last);
	}
}

/*
 * Broadcast slots go to channels picked at random among those missing the block, weight plays no part
 */
TEST (vote_solicitor_round, broadcast_random_slots)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	auto const reps = make_reps (node, 10);
	auto const winner = make_winner ();

	std::set<nano::vote_solicitor_round::channels_t> seen;
	for (uint64_t seed = 0; seed < 50; ++seed)
	{
		nano::vote_solicitor_round round{ reps, { .election_block_broadcasts = 3 }, seed };
		ASSERT_TRUE (round.broadcast_block (make_snapshot (winner)));
		auto const & targets = round.block_broadcasts ()[0].channels;
		ASSERT_EQ (3, targets.size ());
		ASSERT_EQ (3, std::set<std::shared_ptr<nano::transport::channel>> (targets.begin (), targets.end ()).size ());
		seen.insert (targets);
	}
	// Different rounds pick different reps
	ASSERT_GT (seen.size (), 5);
}

/*
 * Over many rounds the random broadcast slots reach every rep missing the block, none far more often than the others
 */
TEST (vote_solicitor_round, broadcast_random_spread)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	auto const reps = make_reps (node, 10);
	auto const winner = make_winner ();

	std::size_t const rounds = 2000;
	std::map<std::shared_ptr<nano::transport::channel>, std::size_t> picks;
	for (uint64_t seed = 0; seed < rounds; ++seed)
	{
		nano::vote_solicitor_round round{ reps, { .election_block_broadcasts = 3 }, seed };
		ASSERT_TRUE (round.broadcast_block (make_snapshot (winner)));
		for (auto const & channel : round.block_broadcasts ()[0].channels)
		{
			++picks[channel];
		}
	}

	// Three random slots shared by ten reps, the heaviest included, wide margins around the mean
	auto const mean = rounds * 3 / 10;
	for (std::size_t i = 0; i < reps.size (); ++i)
	{
		ASSERT_GT (picks[reps[i].channel], mean / 2);
		ASSERT_LT (picks[reps[i].channel], mean * 2);
	}
}

/*
 * Reps that already voted for the winner are never picked for the random broadcast slots
 */
TEST (vote_solicitor_round, broadcast_random_skips_voted)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	auto const reps = make_reps (node, 6);
	auto const winner = make_winner ();

	// Reps 2 to 5 voted for the winner, only reps 1 and 6 still need the block
	nano::election_ballot::vote_map votes;
	for (uint64_t i = 2; i <= 5; ++i)
	{
		votes[nano::account{ i }] = vote_for (winner->hash ());
	}
	for (uint64_t seed = 0; seed < 50; ++seed)
	{
		nano::vote_solicitor_round round{ reps, { .election_block_broadcasts = 3 }, seed };
		ASSERT_TRUE (round.broadcast_block (make_snapshot (winner, votes)));
		ASSERT_EQ ((nano::vote_solicitor_round::channels_t{ reps[0].channel, reps[5].channel }), round.block_broadcasts ()[0].channels);
	}
}

/*
 * Vote requests go heaviest first whatever the seed
 */
TEST (vote_solicitor_round, request_order_seed_independent)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	auto const reps = make_reps (node, 10);
	auto const winner = make_winner ();

	for (uint64_t seed = 0; seed < 20; ++seed)
	{
		nano::vote_solicitor_round round{ reps, { .election_vote_requests = 3 }, seed };
		ASSERT_TRUE (round.request_votes (make_snapshot (winner)));
		ASSERT_EQ (3, round.vote_requests ().size ());
		for (std::size_t i = 0; i < 3; ++i)
		{
			ASSERT_TRUE (round.vote_requests ().contains (reps[i].channel));
		}
	}
}
