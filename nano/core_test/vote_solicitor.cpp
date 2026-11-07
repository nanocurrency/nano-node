#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/stats.hpp>
#include <nano/messages/confirm.hpp>
#include <nano/messages/publish.hpp>
#include <nano/node/election.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/repcrawler.hpp>
#include <nano/node/transport/test_channel.hpp>
#include <nano/node/vote_solicitor.hpp>
#include <nano/secure/vote.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/unordered/unordered_flat_set.hpp>

#include <deque>

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
	ASSERT_EQ (1, round.requested_elections ());
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
	ASSERT_EQ (0, round.requested_elections ());
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
 * A full channel should be left alone for the rest of the round
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

	// The full channel stays dropped for later elections in the round, even once it has room again
	channel_full->unsaturate (nano::transport::traffic_type::confirmation_requests);
	ASSERT_TRUE (round.request_votes (make_snapshot (make_winner (200))));
	ASSERT_FALSE (round.vote_requests ().contains (channel_full));
	ASSERT_EQ (2, round.vote_requests ().at (channel_ok).size ());
}

/*
 * Winner broadcasts are capped per round, a rep that has not voted is targeted
 */
TEST (vote_solicitor_round, broadcast_caps)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	std::size_t const max_broadcasts = 2;
	nano::vote_solicitor_round round{ make_reps (node, 1), { .round_block_broadcasts = max_broadcasts } };

	ASSERT_TRUE (round.broadcast_block (make_snapshot (make_winner (100))));
	ASSERT_TRUE (round.broadcast_block (make_snapshot (make_winner (200))));
	ASSERT_FALSE (round.broadcast_block (make_snapshot (make_winner (300))));

	ASSERT_EQ (max_broadcasts, round.block_broadcasts ().size ());
	ASSERT_EQ (1, round.block_broadcasts ()[0].second.size ());
	ASSERT_TRUE (round.vote_requests ().empty ());
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
		ASSERT_TRUE (round.block_broadcasts ()[0].second.empty ());
	}
	// Voted for a different hash, targeted
	{
		nano::vote_solicitor_round round{ make_reps (node, 1), {} };
		ASSERT_TRUE (round.broadcast_block (make_snapshot (winner, { { nano::account{ 1 }, vote_for (nano::block_hash{ 999 }) } })));
		ASSERT_EQ (1, round.block_broadcasts ()[0].second.size ());
	}
	// The per election cap applies to reps that have not voted, fork voters are exempt
	{
		auto reps = make_reps (node, 4);
		nano::vote_solicitor_round round{ reps, { .election_block_broadcasts = 2 } };
		ASSERT_TRUE (round.broadcast_block (make_snapshot (winner)));
		ASSERT_EQ (2, round.block_broadcasts ()[0].second.size ());

		nano::election_ballot::vote_map votes;
		for (auto const & rep : reps)
		{
			votes[rep.account] = vote_for (nano::block_hash{ 999 });
		}
		ASSERT_TRUE (round.broadcast_block (make_snapshot (winner, votes)));
		ASSERT_EQ (4, round.block_broadcasts ()[1].second.size ());
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
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_solicitor, nano::stat::detail::broadcast, nano::stat::dir::out));
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
	ASSERT_TRUE (round.block_broadcasts ()[0].second.empty ());

	auto const result = node.vote_solicitor.flush (round);
	ASSERT_EQ (boost::unordered_flat_set<nano::block_hash>{ winner->hash () }, result.block_broadcasts);
	ASSERT_EQ (0, node.stats.count (nano::stat::type::vote_solicitor, nano::stat::detail::broadcast, nano::stat::dir::out));
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
	ASSERT_TRUE (round.block_broadcasts ()[0].second.empty ());

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
