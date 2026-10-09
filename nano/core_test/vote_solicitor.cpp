#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/tomlconfig.hpp>
#include <nano/lib/vote.hpp>
#include <nano/messages/confirm.hpp>
#include <nano/messages/publish.hpp>
#include <nano/messages/vote_relay.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/election.hpp>
#include <nano/node/network.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/repcrawler.hpp>
#include <nano/node/transport/test_channel.hpp>
#include <nano/node/vote_rebroadcaster.hpp>
#include <nano/node/vote_relay.hpp>
#include <nano/node/vote_relay_client.hpp>
#include <nano/node/vote_solicitor.hpp>
#include <nano/node/wallet.hpp>
#include <nano/test_common/chains.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <deque>
#include <sstream>

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

nano::election_snapshot make_snapshot (std::shared_ptr<nano::block> const & winner, std::unordered_map<nano::account, nano::vote_info> votes = {}, bool quorum = false)
{
	return { winner->qualified_root (), winner, quorum, std::move (votes) };
}

nano::vote_info vote_for (nano::block_hash const & hash, bool final = false)
{
	return { std::chrono::steady_clock::now (), final ? nano::vote::timestamp_final : nano::vote::timestamp_min, hash };
}

// Reps without channels, the planner never dereferences them
std::vector<nano::representative> make_reps (std::size_t count)
{
	std::vector<nano::representative> reps;
	for (uint64_t i = 1; i <= count; ++i)
	{
		reps.push_back ({ nano::account{ i }, nullptr });
	}
	return reps;
}
}

/*
 * A representative that has not voted should be requested, accumulated per channel with the winner hash and root
 */
TEST (vote_solicitor_round, request_basic)
{
	nano::vote_solicitor_round round{ { make_reps (1) }, {} };

	auto winner = make_winner ();
	ASSERT_TRUE (round.request_votes (make_snapshot (winner)));

	ASSERT_EQ (1, round.vote_requests ().size ());
	auto const & queued = round.vote_requests ().begin ()->second;
	ASSERT_EQ (1, queued.size ());
	ASSERT_EQ (winner->hash (), queued[0].first);
	ASSERT_EQ (winner->root (), queued[0].second);
	ASSERT_TRUE (round.block_broadcasts ().empty ());
}

/*
 * Before quorum any vote for the winner is conclusive, no request is needed
 */
TEST (vote_solicitor_round, skip_voted)
{
	nano::vote_solicitor_round round{ { make_reps (1) }, {} };

	auto winner = make_winner ();
	ASSERT_FALSE (round.request_votes (make_snapshot (winner, { { nano::account{ 1 }, vote_for (winner->hash ()) } })));
	ASSERT_TRUE (round.vote_requests ().empty ());
}

/*
 * After quorum only final votes are conclusive, a non-final voter should be requested again
 */
TEST (vote_solicitor_round, final_upgrade)
{
	auto winner = make_winner ();
	{
		nano::vote_solicitor_round round{ { make_reps (1) }, {} };
		ASSERT_TRUE (round.request_votes (make_snapshot (winner, { { nano::account{ 1 }, vote_for (winner->hash ()) } }, /* quorum */ true)));
	}
	{
		nano::vote_solicitor_round round{ { make_reps (1) }, {} };
		ASSERT_FALSE (round.request_votes (make_snapshot (winner, { { nano::account{ 1 }, vote_for (winner->hash (), /* final */ true) } }, /* quorum */ true)));
	}
}

/*
 * A vote for a different hash should be re-requested without counting towards the per election cap
 */
TEST (vote_solicitor_round, fork_bypasses_cap)
{
	std::size_t const max_requests = 3;
	auto reps = make_reps (max_requests + 1);
	auto winner = make_winner ();

	// All reps voted for a different hash, the cap does not apply
	{
		nano::vote_solicitor_round round{ { reps }, { .election_vote_requests = max_requests } };
		std::unordered_map<nano::account, nano::vote_info> votes;
		for (auto const & rep : reps)
		{
			votes[rep.account] = vote_for (nano::block_hash{ 999 });
		}
		ASSERT_TRUE (round.request_votes (make_snapshot (winner, votes)));
		ASSERT_EQ (max_requests + 1, round.vote_requests ().begin ()->second.size ());
	}
	// No votes at all, the cap applies
	{
		nano::vote_solicitor_round round{ { reps }, { .election_vote_requests = max_requests } };
		ASSERT_TRUE (round.request_votes (make_snapshot (winner)));
		ASSERT_EQ (max_requests, round.vote_requests ().begin ()->second.size ());
	}
}

/*
 * A full channel should be left alone for the rest of the round
 */
TEST (vote_solicitor_round, channel_full)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	auto channel_full = nano::test::fake_channel (node, nano::account{ 1 });
	auto channel_ok = nano::test::fake_channel (node, nano::account{ 2 });

	nano::vote_solicitor_round round{ { { { nano::account{ 1 }, channel_full }, { nano::account{ 2 }, channel_ok } } }, {} };
	round.channel_full = [&] (auto const & channel) {
		return channel == channel_full;
	};

	ASSERT_TRUE (round.request_votes (make_snapshot (make_winner ())));
	ASSERT_EQ (1, round.vote_requests ().size ());
	ASSERT_TRUE (round.vote_requests ().contains (channel_ok));
	ASSERT_FALSE (round.vote_requests ().contains (channel_full));

	// The full channel stays dropped for later elections in the round
	ASSERT_TRUE (round.request_votes (make_snapshot (make_winner (200))));
	ASSERT_FALSE (round.vote_requests ().contains (channel_full));
	ASSERT_EQ (2, round.vote_requests ().at (channel_ok).size ());
}

/*
 * Winner broadcasts are capped per round, a rep that has not voted is targeted
 */
TEST (vote_solicitor_round, broadcast_caps)
{
	std::size_t const max_broadcasts = 2;
	nano::vote_solicitor_round round{ { make_reps (1) }, { .round_block_broadcasts = max_broadcasts } };

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
	auto winner = make_winner ();

	// Voted for the winner, the broadcast still counts as performed but targets nobody
	{
		nano::vote_solicitor_round round{ { make_reps (1) }, {} };
		ASSERT_TRUE (round.broadcast_block (make_snapshot (winner, { { nano::account{ 1 }, vote_for (winner->hash ()) } })));
		ASSERT_EQ (1, round.block_broadcasts ().size ());
		ASSERT_TRUE (round.block_broadcasts ()[0].second.empty ());
	}
	// Voted for a different hash, targeted
	{
		nano::vote_solicitor_round round{ { make_reps (1) }, {} };
		ASSERT_TRUE (round.broadcast_block (make_snapshot (winner, { { nano::account{ 1 }, vote_for (nano::block_hash{ 999 }) } })));
		ASSERT_EQ (1, round.block_broadcasts ()[0].second.size ());
	}
	// The per election cap applies to reps that have not voted, fork voters are exempt
	{
		auto reps = make_reps (4);
		nano::vote_solicitor_round round{ { reps }, { .election_block_broadcasts = 2 } };
		ASSERT_TRUE (round.broadcast_block (make_snapshot (winner)));
		ASSERT_EQ (2, round.block_broadcasts ()[0].second.size ());

		std::unordered_map<nano::account, nano::vote_info> votes;
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

	node.vote_solicitor.flush (round);

	// Both requests are batched into a single confirm_req to the same channel
	ASSERT_EQ (1, confirm_reqs);
	ASSERT_EQ (2, confirm_req_hashes);
	ASSERT_EQ (1, publishes);
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
	for (std::size_t i = 0; i < nano::network::confirm_req_hashes_max + 1; ++i)
	{
		ASSERT_TRUE (round.request_votes (make_snapshot (make_winner (100 + i))));
	}
	node.vote_solicitor.flush (round);

	ASSERT_EQ (2, sizes.size ());
	ASSERT_EQ (nano::network::confirm_req_hashes_max, sizes[0]);
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

namespace
{
nano::relay_rep_selector::candidate make_candidate (uint64_t account, uint64_t weight, bool observed)
{
	return { nano::account{ account }, nano::uint128_t{ weight }, observed };
}
}

/*
 * Observed reps should be picked heaviest first up to their cap, unobserved ones are not considered for those slots
 */
TEST (relay_rep_selector, observed_by_weight)
{
	nano::relay_rep_selector selector{ { .max_reps = 8, .observed = 2, .explore = 0 } };

	auto picked = selector.select ({ make_candidate (1, 10, true), make_candidate (2, 30, true), make_candidate (3, 20, true), make_candidate (4, 100, false) }, std::chrono::steady_clock::now ());
	ASSERT_EQ (2, picked.size ());
	ASSERT_EQ (nano::account{ 2 }, picked[0]);
	ASSERT_EQ (nano::account{ 3 }, picked[1]);
}

/*
 * Unobserved reps should take turns, never asked and then the longest unasked first, heaviest among equals
 */
TEST (relay_rep_selector, explore_rotation)
{
	nano::relay_rep_selector selector{ { .max_reps = 8, .observed = 0, .explore = 1 } };
	auto const now = std::chrono::steady_clock::now ();

	std::deque<nano::relay_rep_selector::candidate> candidates{ make_candidate (1, 30, false), make_candidate (2, 20, false), make_candidate (3, 10, false) };

	ASSERT_EQ (std::deque<nano::account>{ nano::account{ 1 } }, selector.select (candidates, now));
	ASSERT_EQ (std::deque<nano::account>{ nano::account{ 2 } }, selector.select (candidates, now + 1s));
	ASSERT_EQ (std::deque<nano::account>{ nano::account{ 3 } }, selector.select (candidates, now + 2s));
	// Everyone was asked once, the rotation starts over with the longest unasked
	ASSERT_EQ (std::deque<nano::account>{ nano::account{ 1 } }, selector.select (candidates, now + 3s));

	// Reps that drop out of the candidates are forgotten and start fresh when they return, the heavier one goes first again
	ASSERT_EQ (std::deque<nano::account>{ nano::account{ 3 } }, selector.select ({ candidates[2] }, now + 4s));
	ASSERT_EQ (std::deque<nano::account>{ nano::account{ 1 } }, selector.select (candidates, now + 5s));
}

/*
 * Strategy caps are independent, unused slots are not handed over, and the hard cap bounds the total
 */
TEST (relay_rep_selector, caps)
{
	auto const now = std::chrono::steady_clock::now ();
	std::deque<nano::relay_rep_selector::candidate> candidates{ make_candidate (1, 40, true), make_candidate (2, 30, true), make_candidate (3, 20, true), make_candidate (4, 10, false), make_candidate (5, 5, false) };

	{
		nano::relay_rep_selector selector{ { .max_reps = 4, .observed = 2, .explore = 2 } };
		auto picked = selector.select (candidates, now);
		ASSERT_EQ ((std::deque<nano::account>{ nano::account{ 1 }, nano::account{ 2 }, nano::account{ 4 }, nano::account{ 5 } }), picked);

		// With a single observed candidate the spare observed slot stays unused
		auto few = selector.select ({ candidates[0], candidates[3], candidates[4], make_candidate (6, 1, false) }, now + 1s);
		ASSERT_EQ ((std::deque<nano::account>{ nano::account{ 1 }, nano::account{ 6 }, nano::account{ 4 } }), few);
	}
	{
		// The hard cap wins over the strategy caps, observed reps fill it first
		nano::relay_rep_selector selector{ { .max_reps = 2, .observed = 2, .explore = 2 } };
		ASSERT_EQ ((std::deque<nano::account>{ nano::account{ 1 }, nano::account{ 2 } }), selector.select (candidates, now));
	}
	{
		// A rep that starts voting competes by weight in the observed strategy
		nano::relay_rep_selector selector{ { .max_reps = 4, .observed = 1, .explore = 1 } };
		candidates[3].observed = true;
		candidates[3].weight = 50;
		ASSERT_EQ ((std::deque<nano::account>{ nano::account{ 4 }, nano::account{ 5 } }), selector.select (candidates, now));
	}
}

/*
 * Unreachable reps that still need to answer should be grouped into relay requests shared by elections in the same situation
 */
TEST (vote_solicitor_round, relay_request_grouping)
{
	nano::account rep1{ 1 };
	nano::account rep2{ 2 };
	nano::vote_solicitor_round round{ { .direct = {}, .relayed = { rep1, rep2 }, .relays = { nullptr } }, {} };

	auto winner1 = make_winner (100);
	auto winner2 = make_winner (200);
	auto winner3 = make_winner (300);

	// Nobody voted on the first two, both share one request wanting both reps
	ASSERT_TRUE (round.relay_request (make_snapshot (winner1)));
	ASSERT_TRUE (round.relay_request (make_snapshot (winner2)));
	// The first rep already voted on the third, only the second is wanted
	ASSERT_TRUE (round.relay_request (make_snapshot (winner3, { { rep1, vote_for (winner3->hash ()) } })));
	// Everyone voted, nothing to ask
	ASSERT_FALSE (round.relay_request (make_snapshot (winner3, { { rep1, vote_for (winner3->hash ()) }, { rep2, vote_for (winner3->hash ()) } })));

	auto requests = round.relay_requests ();
	ASSERT_EQ (2, requests.size ());

	auto both = std::find_if (requests.begin (), requests.end (), [] (auto const & batch) { return batch.reps.size () == 2; });
	ASSERT_NE (requests.end (), both);
	ASSERT_TRUE (both->include_non_final);
	ASSERT_EQ (2, both->roots_hashes.size ());

	auto single = std::find_if (requests.begin (), requests.end (), [] (auto const & batch) { return batch.reps.size () == 1; });
	ASSERT_NE (requests.end (), single);
	ASSERT_EQ (rep2, single->reps[0]);
	ASSERT_EQ (1, single->roots_hashes.size ());
	ASSERT_EQ (winner3->hash (), single->roots_hashes[0].first);
	ASSERT_EQ (winner3->root (), single->roots_hashes[0].second);

	// Direct requests and broadcasts are untouched
	ASSERT_TRUE (round.vote_requests ().empty ());
	ASSERT_TRUE (round.block_broadcasts ().empty ());
}

/*
 * After quorum only final votes are wanted: a non-final voter is asked again and the request excludes non-final votes
 */
TEST (vote_solicitor_round, relay_request_finality)
{
	nano::account rep{ 1 };
	nano::vote_solicitor_round round{ { .direct = {}, .relayed = { rep }, .relays = { nullptr } }, {} };

	auto winner = make_winner ();
	ASSERT_TRUE (round.relay_request (make_snapshot (winner, { { rep, vote_for (winner->hash ()) } }, /* quorum */ true)));
	ASSERT_FALSE (round.relay_request (make_snapshot (winner, { { rep, vote_for (winner->hash (), /* final */ true) } }, /* quorum */ true)));

	auto requests = round.relay_requests ();
	ASSERT_EQ (1, requests.size ());
	ASSERT_FALSE (requests[0].include_non_final);
	ASSERT_EQ (std::deque<nano::account>{ rep }, requests[0].reps);
}

/*
 * Without relay peers or without unreachable reps no relay request is planned
 */
TEST (vote_solicitor_round, relay_request_none)
{
	auto winner = make_winner ();
	{
		nano::vote_solicitor_round round{ { .direct = {}, .relayed = { nano::account{ 1 } }, .relays = {} }, {} };
		ASSERT_FALSE (round.relay_request (make_snapshot (winner)));
	}
	{
		nano::vote_solicitor_round round{ { .direct = {}, .relayed = {}, .relays = { nullptr } }, {} };
		ASSERT_FALSE (round.relay_request (make_snapshot (winner)));
	}
}

/*
 * Relay requests should go out through the relay client, split at the hash limit and spread over the round's relays
 */
TEST (vote_solicitor, flush_relay)
{
	nano::test::system system;
	nano::node_flags flags;
	flags.disable_rep_crawler = true;
	flags.disable_request_loop = true;
	auto & node = *system.add_node (flags);

	auto relay1 = nano::test::test_channel (node);
	auto relay2 = nano::test::test_channel (node);
	std::deque<std::size_t> sizes1;
	std::deque<std::size_t> sizes2;
	relay1->observe<nano::messages::vote_relay_req> ([&] (nano::messages::vote_relay_req const & message) {
		sizes1.push_back (message.roots_hashes.size ());
	});
	relay2->observe<nano::messages::vote_relay_req> ([&] (nano::messages::vote_relay_req const & message) {
		sizes2.push_back (message.roots_hashes.size ());
	});

	nano::account rep{ 1 };
	nano::vote_solicitor_round round{ { .direct = {}, .relayed = { rep }, .relays = { relay1, relay2 } }, {} };
	for (std::size_t i = 0; i < nano::messages::vote_relay_req::max_hashes + 1; ++i)
	{
		ASSERT_TRUE (round.relay_request (make_snapshot (make_winner (100 + i))));
	}

	node.vote_solicitor.flush (round);

	// One group split into two messages, one per relay
	ASSERT_EQ (std::deque<std::size_t>{ nano::messages::vote_relay_req::max_hashes }, sizes1);
	ASSERT_EQ (std::deque<std::size_t>{ 1 }, sizes2);
	ASSERT_EQ (2, node.stats.count (nano::stat::type::vote_solicitor, nano::stat::detail::relay_request, nano::stat::dir::out));
	ASSERT_EQ (2, node.vote_relay_client.size ());
}

/*
 * Prepare should offer the principal reps without a channel as relay targets once a relay peer is available
 */
TEST (vote_solicitor, prepare_relay_targets)
{
	nano::test::system system;
	nano::node_config relay_config = system.default_config ();
	relay_config.vote_relay->enable = true;
	auto & relay_node = *system.add_node (relay_config);

	// The rep crawler is disabled, so the node never learns a channel to any representative
	nano::node_flags flags;
	flags.disable_rep_crawler = true;
	flags.disable_request_loop = true;
	auto & node = *system.add_node (system.default_config (), flags);

	ASSERT_TIMELY (5s, !node.vote_relay_client.relays (1).empty ());
	ASSERT_TIMELY (5s, !node.vote_solicitor.prepare ().relay_reps ().empty ());

	auto round = node.vote_solicitor.prepare ();
	ASSERT_EQ (1, round.relays ().size ());
	ASSERT_EQ (relay_node.get_node_id (), round.relays ()[0]->get_node_id ());
	ASSERT_EQ (std::deque<nano::account>{ nano::dev::genesis_key.pub }, round.relay_reps ());

	// The relay itself has no relay peers and plans nothing
	ASSERT_TRUE (relay_node.vote_solicitor.prepare ().relays ().empty ());
	ASSERT_TRUE (relay_node.vote_solicitor.prepare ().relay_reps ().empty ());

	// A representative with a channel is asked directly and drops out of the relay targets
	node.rep_crawler.force_add_rep (nano::dev::genesis_key.pub, nano::test::test_channel (node));
	ASSERT_TRUE (node.vote_solicitor.prepare ().relay_reps ().empty ());
	ASSERT_EQ (1, node.vote_solicitor.prepare ().relays ().size ());
}

/*
 * A node without a channel to the representative should confirm through a relay: the relay queries the representative and forwards its vote
 */
TEST (vote_solicitor, relay_end_to_end)
{
	nano::test::system system;

	// Voting representative, its block is confirmed before the other nodes join so it only votes when asked
	nano::node_config rep_config = system.default_config ();
	rep_config.enable_voting = true;
	auto & rep_node = *system.add_node (rep_config);
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	rep_node.wallets.refresh_reps ();

	auto blocks = nano::test::setup_chain (system, rep_node, 1);
	ASSERT_TIMELY (5s, rep_node.active.empty ());

	// Relay, learns the representative over the network and keeps the votes it receives to itself
	nano::node_config relay_config = system.default_config ();
	relay_config.vote_relay->enable = true;
	relay_config.vote_rebroadcaster->enable = false;
	auto & relay_node = *system.add_node (relay_config);
	ASSERT_TIMELY_EQ (10s, relay_node.rep_crawler.representative_count (), 1);

	// Requester, never learns a channel to the representative
	nano::node_flags flags;
	flags.disable_rep_crawler = true;
	auto & node = *system.add_node (system.default_config (), flags);
	ASSERT_TIMELY (5s, !node.vote_relay_client.relays (1).empty ());

	// Put the block in the ledger regardless of bootstrap timing and start its election, the only way to a vote is through the relay
	auto const status = node.process (blocks[0]);
	ASSERT_TRUE (status == nano::block_status::progress || status == nano::block_status::old);
	auto election = nano::test::start_election (system, node, blocks[0]->hash ());
	ASSERT_NE (nullptr, election);

	ASSERT_TIMELY (10s, election->confirmed ());
	ASSERT_EQ (0, election->confirmation_request_count); // Nobody to ask directly
	ASSERT_GE (node.stats.count (nano::stat::type::election, nano::stat::detail::relay_request), 1);
	ASSERT_GE (node.stats.count (nano::stat::type::vote_solicitor, nano::stat::detail::relay_request, nano::stat::dir::out), 1);
	ASSERT_GE (node.stats.count (nano::stat::type::vote_relay_client, nano::stat::detail::vote), 1);
	ASSERT_GE (node.stats.count (nano::stat::type::vote_processor_source, nano::stat::detail::relay), 1);
	ASSERT_GE (relay_node.stats.count (nano::stat::type::vote_relay, nano::stat::detail::request), 1);
}

/*
 * The relay caps must be consistent: the strategy caps fit under the hard cap and the hard cap under the wire limit
 */
TEST (vote_solicitor_config, validation)
{
	auto deserialize = [] (std::string const & text) {
		std::stringstream ss;
		ss << text;
		nano::tomlconfig toml;
		toml.read (ss);
		nano::vote_solicitor_config config;
		return config.deserialize (toml);
	};

	ASSERT_FALSE (deserialize ("relay_reps = 32\nrelay_reps_observed = 28\nrelay_reps_explore = 4\n"));
	// The strategy caps together exceed the hard cap
	ASSERT_TRUE (deserialize ("relay_reps = 32\nrelay_reps_observed = 30\nrelay_reps_explore = 4\n"));
	// The hard cap exceeds what fits in a request
	ASSERT_TRUE (deserialize ("relay_reps = 300\nrelay_reps_observed = 1\nrelay_reps_explore = 1\n"));
}
