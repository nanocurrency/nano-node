#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/enum_util.hpp>
#include <nano/lib/stats.hpp>
#include <nano/messages/asc_pull.hpp>
#include <nano/node/block_rebroadcaster.hpp>
#include <nano/node/bootstrap/bootstrap_config.hpp>
#include <nano/node/bootstrap/bootstrap_server.hpp>
#include <nano/node/bootstrap/bootstrap_service.hpp>
#include <nano/node/bootstrap/ondemand_strategy.hpp>
#include <nano/node/fork_cache.hpp>
#include <nano/node/local_block_broadcaster.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/rep_tiers.hpp>
#include <nano/node/scheduler/component.hpp>
#include <nano/node/scheduler/hinted.hpp>
#include <nano/node/transport/inproc.hpp>
#include <nano/node/unchecked_map.hpp>
#include <nano/node/vote_cache.hpp>
#include <nano/node/vote_processor.hpp>
#include <nano/node/wallet.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/vote.hpp>
#include <nano/test_common/random.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>

using namespace std::chrono_literals;

namespace
{
// Config of a node that only serves blocks: it floods none and pulls none, so a peer gets a block from it by request alone
nano::node_config server_config (nano::test::system & system)
{
	auto config = system.default_config ();
	config.block_rebroadcaster->enable = false;
	config.bootstrap->enable = false;
	return config;
}

// Config of a node whose only way to obtain a block is the on-demand strategy, with a delay and a cooldown short enough for a test
nano::node_config client_config (nano::test::system & system)
{
	auto config = system.default_config ();
	config.block_rebroadcaster->enable = false;
	config.bootstrap->enable_priorities = false;
	config.bootstrap->enable_database_scan = false;
	config.bootstrap->enable_dependency_walker = false;
	config.bootstrap->enable_frontier_scan = false;
	config.bootstrap->enable_topo_scan = false;
	config.bootstrap->ondemand.delay = 100ms;
	config.bootstrap->ondemand.retry_cooldown = 50ms;
	// Long enough that a slow machine does not turn one request into two
	config.bootstrap->ondemand.request_timeout = 10s;
	return config;
}

// A send from the genesis account to `destination`, on top of `previous`, leaving `balance`
std::shared_ptr<nano::block> make_genesis_send (nano::test::system & system, nano::block_hash const & previous, nano::uint128_t const & balance, nano::account const & destination)
{
	nano::state_block_builder builder;
	return builder.make_block ()
	.account (nano::dev::genesis_key.pub)
	.previous (previous)
	.representative (nano::dev::genesis_key.pub)
	.link (destination)
	.balance (balance)
	.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
	.work (*system.work.generate (previous))
	.build ();
}

// What a misbehaving peer hands over in place of the blocks of a genuine response
using forge_t = std::function<std::deque<std::shared_ptr<nano::block>> (std::deque<std::shared_ptr<nano::block>> const &)>;

// Makes `server` misbehave once: the first blocks response it prepares reaches `client` forged, under the same id and ahead of the genuine one, which then finds no request left to answer
// `forged` has to be declared ahead of the system, so that the server's observer never outlives it
void forge_first_response (nano::node & server, nano::node & client, std::atomic<bool> & forged, forge_t forge)
{
	server.bootstrap_server.on_response.add ([&client, &forged, forge] (nano::messages::asc_pull_ack const & response, auto const &) {
		if (!std::holds_alternative<nano::messages::asc_pull_ack::blocks_payload> (response.payload))
		{
			return;
		}
		if (forged.exchange (true))
		{
			return;
		}
		nano::messages::asc_pull_ack::blocks_payload payload;
		payload.blocks = forge (std::get<nano::messages::asc_pull_ack::blocks_payload> (response.payload).blocks);
		nano::messages::asc_pull_ack message{ client.network_params.network };
		message.id = response.id;
		message.type = nano::messages::asc_pull_type::blocks;
		message.payload = payload;
		message.update_header ();
		client.bootstrap.process (message, nano::test::fake_channel (client));
	});
}

// The first block of `key`'s account, receiving `amount` from `send`
std::shared_ptr<nano::block> make_open (nano::test::system & system, nano::keypair const & key, std::shared_ptr<nano::block> const & send, nano::uint128_t const & amount)
{
	nano::state_block_builder builder;
	return builder.make_block ()
	.account (key.pub)
	.previous (0)
	.representative (key.pub)
	.link (send->hash ())
	.balance (amount)
	.sign (key.prv, key.pub)
	.work (*system.work.generate (key.pub))
	.build ();
}
}

/*
 * A demanded block that a peer holds is fetched with a single request and enters the ledger.
 * The serving node floods nothing and the requesting node runs no other bootstrap strategy, so the block can only arrive through the on-demand request.
 */
TEST (bootstrap_ondemand, fetch_demanded_block)
{
	nano::test::system system;
	auto & server = *system.add_node (server_config (system));
	auto & client = *system.add_node (client_config (system));

	// Only the serving node holds the block
	auto const send = make_genesis_send (system, nano::dev::genesis->hash (), nano::dev::constants.genesis_amount - 1, nano::keypair{}.pub);
	ASSERT_EQ (nano::block_status::progress, server.process (send));
	ASSERT_EQ (nullptr, client.block (send->hash ()));

	// Nothing is requested before the block is demanded
	ASSERT_EQ (0, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::request));

	client.bootstrap.demand (send->hash ());

	// The block arrives and the strategy sees it enter the ledger
	ASSERT_TIMELY (5s, client.block (send->hash ()) != nullptr);
	ASSERT_TIMELY_EQ (5s, 1, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::resolved));

	// It took exactly one request, answered with the block
	ASSERT_EQ (1, client.stats.count (nano::stat::type::bootstrap_ondemand_insert, nano::stat::detail::inserted));
	ASSERT_EQ (1, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::request));
	ASSERT_EQ (1, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::fetched));
	ASSERT_EQ (0, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::empty));
}

/*
 * With the strategy disabled a demand is ignored: nothing is tracked and nothing is requested.
 */
TEST (bootstrap_ondemand, disabled)
{
	nano::test::system system;
	auto & server = *system.add_node (server_config (system));
	auto config = client_config (system);
	config.bootstrap->enable_ondemand = false;
	auto & client = *system.add_node (config);

	// The serving node holds a block the other node could fetch
	auto const send = make_genesis_send (system, nano::dev::genesis->hash (), nano::dev::constants.genesis_amount - 1, nano::keypair{}.pub);
	ASSERT_EQ (nano::block_status::progress, server.process (send));

	client.bootstrap.demand (send->hash ());

	// The block never arrives and the demand left no trace
	ASSERT_ALWAYS (1s, client.block (send->hash ()) == nullptr);
	ASSERT_EQ (0, client.stats.count (nano::stat::type::bootstrap_ondemand_insert, nano::stat::detail::inserted));
	ASSERT_EQ (0, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::request));
}

/*
 * A demanded block that arrives on its own within the delay is not requested.
 * This is the common case on a healthy network, where votes for a block can reach a node shortly before the block does.
 */
TEST (bootstrap_ondemand, arrival_within_delay)
{
	nano::test::system system;
	auto & server = *system.add_node (server_config (system));
	auto config = client_config (system);
	config.bootstrap->ondemand.delay = 2s;
	auto & client = *system.add_node (config);

	// The serving node holds the block, so a request for it would succeed
	auto const send = make_genesis_send (system, nano::dev::genesis->hash (), nano::dev::constants.genesis_amount - 1, nano::keypair{}.pub);
	ASSERT_EQ (nano::block_status::progress, server.process (send));

	// The block is demanded and then arrives through the block processor while the delay is still running
	client.bootstrap.demand (send->hash ());
	client.process_active (send);
	ASSERT_TIMELY_EQ (5s, 1, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::resolved));

	// No request goes out, not even once the delay has passed
	ASSERT_ALWAYS_EQ (2500ms, 0, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::request));
}

/*
 * A demanded block that is in the ledger by the time it is due is dropped without a request.
 * The block is written to the ledger directly, so the strategy never sees it processed and only finds it when it checks the ledger.
 */
TEST (bootstrap_ondemand, block_in_ledger)
{
	nano::test::system system;
	auto & server = *system.add_node (server_config (system));
	auto & client = *system.add_node (client_config (system));

	// Both nodes hold the block
	auto const send = make_genesis_send (system, nano::dev::genesis->hash (), nano::dev::constants.genesis_amount - 1, nano::keypair{}.pub);
	ASSERT_EQ (nano::block_status::progress, server.process (send));
	ASSERT_EQ (nano::block_status::progress, client.process (send));

	client.bootstrap.demand (send->hash ());

	// The demand is dropped at the ledger check and nothing is requested
	ASSERT_TIMELY_EQ (5s, 1, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::old));
	ASSERT_ALWAYS_EQ (500ms, 0, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::request));
}

/*
 * A demanded block that the node already holds as unchecked, waiting for its predecessor, is not requested.
 * Such a block is not in the ledger, and without this check it would be fetched again every time it is demanded.
 */
TEST (bootstrap_ondemand, block_in_unchecked)
{
	nano::test::system system;
	auto & server = *system.add_node (server_config (system));
	auto & client = *system.add_node (client_config (system));

	// The serving node holds a chain of two blocks
	auto const send1 = make_genesis_send (system, nano::dev::genesis->hash (), nano::dev::constants.genesis_amount - 1, nano::keypair{}.pub);
	auto const send2 = make_genesis_send (system, send1->hash (), nano::dev::constants.genesis_amount - 2, nano::keypair{}.pub);
	ASSERT_EQ (nano::block_status::progress, server.process (send1));
	ASSERT_EQ (nano::block_status::progress, server.process (send2));

	// The other node receives the second block only, which parks it as unchecked
	client.process_active (send2);
	ASSERT_TIMELY (5s, client.unchecked.contains_block (send2->hash ()));

	client.bootstrap.demand (send2->hash ());

	// The demand stops at the unchecked check and nothing is requested
	ASSERT_TIMELY_EQ (5s, 1, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::unchecked));
	ASSERT_ALWAYS_EQ (500ms, 0, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::request));
}

/*
 * A block that no peer holds is requested the configured number of times and then left alone.
 * The only peer answers every request with an empty response, so each attempt goes to it again once the peers are exhausted,
 * and after the last attempt neither time nor a repeated demand produces another request.
 */
TEST (bootstrap_ondemand, abandon_block_nobody_holds)
{
	nano::test::system system;
	system.add_node (server_config (system));
	auto config = client_config (system);
	config.bootstrap->ondemand.max_attempts = 3;
	auto & client = *system.add_node (config);

	// A hash that belongs to no block anywhere
	auto const hash = nano::test::random_hash ();

	client.bootstrap.demand (hash);

	// Three requests, each answered with nothing, after which the block is abandoned
	ASSERT_TIMELY_EQ (10s, 1, client.stats.count (nano::stat::type::bootstrap_ondemand_fail, nano::stat::detail::abandoned));
	ASSERT_EQ (3, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::request));
	ASSERT_EQ (3, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::empty));
	ASSERT_EQ (2, client.stats.count (nano::stat::type::bootstrap_ondemand_fail, nano::stat::detail::retry));

	// Demanding it again is a duplicate and no further request is sent
	client.bootstrap.demand (hash);
	ASSERT_EQ (1, client.stats.count (nano::stat::type::bootstrap_ondemand_insert, nano::stat::detail::duplicate));
	ASSERT_ALWAYS_EQ (1s, 3, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::request));
}

/*
 * A peer that does not answer is given up on after the request timeout, long before the request tag would expire.
 * The serving node has its bootstrap server switched off, so requests to it get no response at all.
 */
TEST (bootstrap_ondemand, timeout_unanswered_request)
{
	nano::test::system system;
	auto silent_config = server_config (system);
	silent_config.bootstrap_server->enable = false;
	auto & server = *system.add_node (silent_config);
	auto config = client_config (system);
	config.bootstrap->ondemand.request_timeout = 500ms;
	config.bootstrap->ondemand.max_attempts = 2;
	auto & client = *system.add_node (config);

	// The silent node holds the block but will not serve it
	auto const send = make_genesis_send (system, nano::dev::genesis->hash (), nano::dev::constants.genesis_amount - 1, nano::keypair{}.pub);
	ASSERT_EQ (nano::block_status::progress, server.process (send));

	client.bootstrap.demand (send->hash ());

	// Both requests time out and the block is abandoned, well within the 15 seconds a request tag lives
	ASSERT_TIMELY_EQ (5s, 1, client.stats.count (nano::stat::type::bootstrap_ondemand_fail, nano::stat::detail::abandoned));
	ASSERT_EQ (2, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::request));
	ASSERT_EQ (2, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::timeout));
	ASSERT_EQ (nullptr, client.block (send->hash ()));
}

/*
 * When the first peer does not hold the block, the block is fetched from another one.
 * The node starts with a single peer that lacks the block. A second peer that holds it joins only after the first has answered with nothing.
 */
TEST (bootstrap_ondemand, fetch_from_another_peer)
{
	nano::test::system system;
	system.add_node (server_config (system));
	auto config = client_config (system);
	config.bootstrap->ondemand.max_attempts = 10;
	auto & client = *system.add_node (config);

	// A block that neither node holds yet
	auto const send = make_genesis_send (system, nano::dev::genesis->hash (), nano::dev::constants.genesis_amount - 1, nano::keypair{}.pub);

	client.bootstrap.demand (send->hash ());

	// The only peer answers that it does not hold the block
	ASSERT_TIMELY (5s, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::empty) >= 1);

	// A peer that holds the block joins the network
	auto & holding = *system.add_node (server_config (system));
	ASSERT_EQ (nano::block_status::progress, holding.process (send));

	// One of the remaining attempts reaches it and delivers the block
	ASSERT_TIMELY (10s, client.block (send->hash ()) != nullptr);
	ASSERT_TIMELY_EQ (5s, 1, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::resolved));
	ASSERT_EQ (1, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::fetched));
}

/*
 * A fetched block that is a fork of a block in the ledger is handed over once and not requested again.
 * It cannot enter the ledger, so it stays missing from it, but it reaches the fork cache where an election can pick it up.
 */
TEST (bootstrap_ondemand, fork_fetched_once)
{
	nano::test::system system;
	auto & server = *system.add_node (server_config (system));
	auto & client = *system.add_node (client_config (system));

	// The two nodes hold different blocks for the same root
	auto const fork1 = make_genesis_send (system, nano::dev::genesis->hash (), nano::dev::constants.genesis_amount - 1, nano::keypair{}.pub);
	auto const fork2 = make_genesis_send (system, nano::dev::genesis->hash (), nano::dev::constants.genesis_amount - 1, nano::keypair{}.pub);
	ASSERT_EQ (nano::block_status::progress, client.process (fork1));
	ASSERT_EQ (nano::block_status::progress, server.process (fork2));

	client.bootstrap.demand (fork2->hash ());

	// The fork is fetched, refused by the ledger and kept in the fork cache
	ASSERT_TIMELY_EQ (5s, 1, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::fork));
	ASSERT_TIMELY (5s, client.fork_cache.contains (fork2->qualified_root ()));
	ASSERT_EQ (nullptr, client.block (fork2->hash ()));

	// Although the block is still missing from the ledger, neither time nor a repeated demand requests it again
	client.bootstrap.demand (fork2->hash ());
	ASSERT_EQ (1, client.stats.count (nano::stat::type::bootstrap_ondemand_insert, nano::stat::detail::duplicate));
	ASSERT_ALWAYS_EQ (1s, 1, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::request));
}

/*
 * A fetched block whose predecessor is missing too hands its account over to the priority strategy, which pulls the chain.
 * The on-demand strategy fetches only the block it was asked for; the rest of the account is not its job.
 */
TEST (bootstrap_ondemand, missing_predecessor_prioritizes_account)
{
	nano::test::system system;
	auto & server = *system.add_node (server_config (system));
	auto config = client_config (system);
	config.bootstrap->enable_priorities = true;
	auto & client = *system.add_node (config);

	// An account with two blocks: it receives a send from genesis and then sends on
	nano::keypair key;
	nano::state_block_builder builder;
	auto const send = make_genesis_send (system, nano::dev::genesis->hash (), nano::dev::constants.genesis_amount - 2, key.pub);
	auto const open = builder.make_block ()
					  .account (key.pub)
					  .previous (0)
					  .representative (key.pub)
					  .link (send->hash ())
					  .balance (2)
					  .sign (key.prv, key.pub)
					  .work (*system.work.generate (key.pub))
					  .build ();
	auto const forward = builder.make_block ()
						 .account (key.pub)
						 .previous (open->hash ())
						 .representative (key.pub)
						 .link (nano::keypair{}.pub)
						 .balance (1)
						 .sign (key.prv, key.pub)
						 .work (*system.work.generate (open->hash ()))
						 .build ();

	// The serving node holds all of it, the other node only the genesis send, so nothing points it at the new account
	ASSERT_EQ (nano::block_status::progress, server.process (send));
	ASSERT_EQ (nano::block_status::progress, server.process (open));
	ASSERT_EQ (nano::block_status::progress, server.process (forward));
	ASSERT_EQ (nano::block_status::progress, client.process (send));

	// The second block of the account is demanded while its predecessor is missing
	client.bootstrap.demand (forward->hash ());

	// It is fetched and cannot be applied
	ASSERT_TIMELY_EQ (5s, 1, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::gap_previous));

	// The priority strategy then pulls the account, after which both of its blocks are in the ledger
	ASSERT_TIMELY (10s, client.block (open->hash ()) != nullptr);
	ASSERT_TIMELY (10s, client.block (forward->hash ()) != nullptr);

	// The on-demand strategy itself sent a single request
	ASSERT_EQ (1, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::request));
}

/*
 * Requests are paced by the strategy's rate limit.
 * With a limit of two requests per second, which also allows a burst of two, four demanded blocks cannot all be requested in under a second.
 */
TEST (bootstrap_ondemand, rate_limit)
{
	nano::test::system system;
	auto & server = *system.add_node (server_config (system));
	auto config = client_config (system);
	config.bootstrap->ondemand_rate_limit = 2;
	auto & client = *system.add_node (config);

	// A chain of four blocks that only the serving node holds, so each of them takes exactly one request
	std::deque<std::shared_ptr<nano::block>> blocks;
	auto previous = nano::dev::genesis->hash ();
	for (auto n = 1; n <= 4; ++n)
	{
		auto const block = make_genesis_send (system, previous, nano::dev::constants.genesis_amount - n, nano::keypair{}.pub);
		ASSERT_EQ (nano::block_status::progress, server.process (block));
		blocks.push_back (block);
		previous = block->hash ();
	}

	// All four are demanded at once
	auto const start = std::chrono::steady_clock::now ();
	for (auto const & block : blocks)
	{
		client.bootstrap.demand (block->hash ());
	}

	// Every block is fetched, which takes at least the time the limiter needs to hand out two more requests
	ASSERT_TIMELY_EQ (10s, 4, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::fetched));
	auto const elapsed = std::chrono::steady_clock::now () - start;
	ASSERT_GE (elapsed, 900ms);
	ASSERT_EQ (4, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::request));

	// Fetched out of order, the blocks still all end up in the ledger
	ASSERT_TIMELY (10s, client.block (blocks.back ()->hash ()) != nullptr);
}

/*
 * A node that receives the votes for a block but never the block itself fetches it and confirms it.
 * This is the case the strategy exists for: block flooding skips a node now and then, while the votes still reach it.
 * The final vote of the genesis representative is all the node gets; the block is fetched on its strength and the vote, cached in the meantime, confirms it.
 */
TEST (bootstrap_ondemand, voted_block_fetched_and_confirmed)
{
	nano::test::system system;
	auto & server = *system.add_node (server_config (system));
	auto & client = *system.add_node (client_config (system));

	// Only the serving node holds the block
	auto const send = make_genesis_send (system, nano::dev::genesis->hash (), nano::dev::constants.genesis_amount - 1, nano::keypair{}.pub);
	ASSERT_EQ (nano::block_status::progress, server.process (send));

	// Votes are cached for principal representatives only, which the genesis representative is once the tiers are computed
	ASSERT_TIMELY (5s, client.rep_tiers.tier (nano::dev::genesis_key.pub) != nano::rep_tier::none);

	// The vote arrives without the block
	auto const vote = nano::test::make_final_vote (nano::dev::genesis_key, std::vector<nano::block_hash>{ send->hash () });
	client.vote_processor.vote (vote, std::make_shared<nano::transport::inproc::channel> (client, client));

	// The node fetches the block with a single request
	ASSERT_TIMELY (5s, client.block (send->hash ()) != nullptr);
	ASSERT_EQ (1, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::request));

	// And the cached vote confirms it
	ASSERT_TIMELY (10s, nano::test::confirmed (client, { send->hash () }));
}

/*
 * Votes make a block wanted only once their weight reaches the hinting threshold.
 * A representative holding half of the threshold votes first, which must not cause a request; anyone could otherwise make
 * the node issue requests with a vote from a minor representative. The genesis representative's vote then carries the tally over the threshold.
 */
TEST (bootstrap_ondemand, votes_below_threshold_ignored)
{
	nano::test::system system;
	auto & server = *system.add_node (server_config (system));
	auto & client = *system.add_node (client_config (system));

	// A second representative with half of the weight it takes to act on cached votes, known to both nodes
	nano::keypair rep;
	auto const weight = client.scheduler.hinted.tally_threshold () / 2;
	nano::state_block_builder builder;
	auto const fund = make_genesis_send (system, nano::dev::genesis->hash (), nano::dev::constants.genesis_amount - weight, rep.pub);
	auto const open = builder.make_block ()
					  .account (rep.pub)
					  .previous (0)
					  .representative (rep.pub)
					  .link (fund->hash ())
					  .balance (weight)
					  .sign (rep.prv, rep.pub)
					  .work (*system.work.generate (rep.pub))
					  .build ();
	ASSERT_TRUE (nano::test::process (server, { fund, open }));
	ASSERT_TRUE (nano::test::process (client, { fund, open }));
	ASSERT_TIMELY (5s, client.rep_tiers.tier (rep.pub) != nano::rep_tier::none);
	ASSERT_TIMELY (5s, client.rep_tiers.tier (nano::dev::genesis_key.pub) != nano::rep_tier::none);

	// A block that only the serving node holds
	auto const send = make_genesis_send (system, fund->hash (), nano::dev::constants.genesis_amount - weight - 1, nano::keypair{}.pub);
	ASSERT_EQ (nano::block_status::progress, server.process (send));

	// The minor representative's vote is cached but makes nothing wanted
	auto const minor_vote = nano::test::make_final_vote (rep, std::vector<nano::block_hash>{ send->hash () });
	client.vote_processor.vote (minor_vote, std::make_shared<nano::transport::inproc::channel> (client, client));
	ASSERT_TIMELY (5s, client.vote_cache.contains (send->hash ()));
	ASSERT_ALWAYS_EQ (1s, 0, client.stats.count (nano::stat::type::bootstrap_ondemand_insert, nano::stat::detail::inserted));

	// The genesis representative's vote brings the tally over the threshold and the block is fetched
	auto const major_vote = nano::test::make_final_vote (nano::dev::genesis_key, std::vector<nano::block_hash>{ send->hash () });
	client.vote_processor.vote (major_vote, std::make_shared<nano::transport::inproc::channel> (client, client));
	ASSERT_TIMELY (5s, client.block (send->hash ()) != nullptr);
	ASSERT_EQ (1, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::request));
}

/*
 * A missing block whose demand was never raised by a vote is picked up by the hinted scheduler's scan of the vote cache.
 * The votes are put into the cache directly, which reports nothing, so the scan is the only thing that can notice the block is missing.
 */
TEST (bootstrap_ondemand, hinted_scan_demands_missing_block)
{
	nano::test::system system;
	auto & server = *system.add_node (server_config (system));
	auto & client = *system.add_node (client_config (system));

	// Only the serving node holds the block
	auto const send = make_genesis_send (system, nano::dev::genesis->hash (), nano::dev::constants.genesis_amount - 1, nano::keypair{}.pub);
	ASSERT_EQ (nano::block_status::progress, server.process (send));

	// Enough vote weight for the block is put into the cache by the route that reports nothing
	auto const vote = nano::test::make_final_vote (nano::dev::genesis_key, std::vector<nano::block_hash>{ send->hash () });
	client.vote_cache.insert (vote, nano::dev::constants.genesis_amount);

	// The scan finds the block missing and demands it, after which it is fetched with a single request
	ASSERT_TIMELY (5s, client.stats.count (nano::stat::type::hinting, nano::stat::detail::missing_block) >= 1);
	ASSERT_TIMELY (5s, client.block (send->hash ()) != nullptr);
	ASSERT_EQ (1, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::request));
}

/*
 * The source of a block is fetched on demand only when the block misses it and either arrived outside of a sync,
 * pushed by a peer or back from the unchecked blocks, or was itself fetched on demand because representatives voted for it.
 * A block fetched on demand as a dependency does not pass its demand on, and blocks pulled by the other strategies are left to the blocked accounts.
 */
TEST (bootstrap_ondemand, demands_source)
{
	using nano::bootstrap::ondemand_index;

	for (auto const result : nano::enum_values<nano::block_status> ())
	{
		bool const gap = result == nano::block_status::gap_source;

		for (auto const source : nano::enum_values<nano::block_source> ())
		{
			// Not fetched on demand: the block source decides
			bool const pushed = source == nano::block_source::live || source == nano::block_source::live_originator;
			bool const outside_sync = pushed || source == nano::block_source::unchecked;
			ASSERT_EQ (nano::bootstrap::demands_source (result, source, std::nullopt), gap && outside_sync) << nano::to_string (result) << " " << nano::to_string (source);

			// Fetched on demand: what the block was fetched for decides, whatever its source
			ASSERT_EQ (nano::bootstrap::demands_source (result, source, ondemand_index::reason::votes), gap) << nano::to_string (result) << " " << nano::to_string (source);
			ASSERT_FALSE (nano::bootstrap::demands_source (result, source, ondemand_index::reason::dependency)) << nano::to_string (result) << " " << nano::to_string (source);
		}
	}
}

/*
 * A live block whose source is missing makes the node fetch the source, after which both enter the ledger.
 * This is what a node meets after it was away: it has no votes for a send it missed, but the block that receives the send still arrives.
 */
TEST (bootstrap_ondemand, live_block_missing_source)
{
	nano::test::system system;
	auto & server = *system.add_node (server_config (system));
	auto & client = *system.add_node (client_config (system));

	// A send from genesis and the block that receives it into a new account, both held by the serving node only
	nano::keypair key;
	auto const send = make_genesis_send (system, nano::dev::genesis->hash (), nano::dev::constants.genesis_amount - 1, key.pub);
	auto const open = make_open (system, key, send, 1);
	ASSERT_TRUE (nano::test::process (server, { send, open }));

	// The receiving block arrives live without the send it depends on
	client.process_active (open);

	// The send is wanted as a dependency and fetched with a single request, which lets the receiving block through
	ASSERT_TIMELY (5s, client.block (send->hash ()) != nullptr);
	ASSERT_TIMELY (5s, client.block (open->hash ()) != nullptr);
	ASSERT_EQ (1, client.stats.count (nano::stat::type::bootstrap_ondemand_insert, nano::stat::detail::inserted));
	ASSERT_EQ (1, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::request));
}

/*
 * A block fetched on demand because representatives voted for it has its missing source fetched as well.
 * The node holds neither a send nor the block that receives it; only the receiving block is demanded.
 */
TEST (bootstrap_ondemand, fetched_block_missing_source)
{
	nano::test::system system;
	auto & server = *system.add_node (server_config (system));
	auto & client = *system.add_node (client_config (system));

	// A send from genesis and the block that receives it into a new account, both held by the serving node only
	nano::keypair key;
	auto const send = make_genesis_send (system, nano::dev::genesis->hash (), nano::dev::constants.genesis_amount - 1, key.pub);
	auto const open = make_open (system, key, send, 1);
	ASSERT_TRUE (nano::test::process (server, { send, open }));

	client.bootstrap.demand (open->hash ());

	// The receiving block is fetched and found to miss its source
	ASSERT_TIMELY_EQ (5s, 1, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::gap_source));

	// The send is fetched in turn and both blocks enter the ledger, at one request each
	ASSERT_TIMELY (5s, client.block (send->hash ()) != nullptr);
	ASSERT_TIMELY (5s, client.block (open->hash ()) != nullptr);
	ASSERT_EQ (2, client.stats.count (nano::stat::type::bootstrap_ondemand_insert, nano::stat::detail::inserted));
	ASSERT_EQ (2, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::request));
}

/*
 * With bootstrap switched off as a whole a demand is ignored, even though the strategy's own switch is on.
 * Nothing would request or expire a tracked block in that state, so none is tracked.
 */
TEST (bootstrap_ondemand, bootstrap_disabled)
{
	nano::test::system system;
	auto & server = *system.add_node (server_config (system));
	auto config = client_config (system);
	config.bootstrap->enable = false;
	auto & client = *system.add_node (config);

	// The serving node holds a block the other node could fetch
	auto const send = make_genesis_send (system, nano::dev::genesis->hash (), nano::dev::constants.genesis_amount - 1, nano::keypair{}.pub);
	ASSERT_EQ (nano::block_status::progress, server.process (send));

	client.bootstrap.demand (send->hash ());

	// The block never arrives and the demand left no trace
	ASSERT_ALWAYS (1s, client.block (send->hash ()) == nullptr);
	ASSERT_EQ (0, client.stats.count (nano::stat::type::bootstrap_ondemand_insert, nano::stat::detail::inserted));
	ASSERT_EQ (0, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::request));
}

/*
 * Resetting bootstrap forgets the wanted blocks: a block demanded before the reset is never requested.
 */
TEST (bootstrap_ondemand, reset)
{
	nano::test::system system;
	auto & server = *system.add_node (server_config (system));
	auto config = client_config (system);
	config.bootstrap->ondemand.delay = 1s;
	auto & client = *system.add_node (config);

	// The serving node holds the block, so a request for it would succeed
	auto const send = make_genesis_send (system, nano::dev::genesis->hash (), nano::dev::constants.genesis_amount - 1, nano::keypair{}.pub);
	ASSERT_EQ (nano::block_status::progress, server.process (send));

	// The block is demanded and bootstrap is reset while the delay is still running
	client.bootstrap.demand (send->hash ());
	ASSERT_EQ (1, client.stats.count (nano::stat::type::bootstrap_ondemand_insert, nano::stat::detail::inserted));
	client.bootstrap.reset ();

	// No request goes out once the delay has passed, and the block stays missing
	ASSERT_ALWAYS_EQ (1500ms, 0, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::request));
	ASSERT_EQ (nullptr, client.block (send->hash ()));
}

/*
 * A request whose tag expires before the strategy's own timeout fails the request just the same.
 * The peer never answers, the strategy's timeout is set far out of reach and the tag's is short, so only the expiring tags can fail the two requests.
 */
TEST (bootstrap_ondemand, tag_timeout)
{
	nano::test::system system;
	auto silent_config = server_config (system);
	silent_config.bootstrap_server->enable = false;
	auto & server = *system.add_node (silent_config);
	auto config = client_config (system);
	config.bootstrap->request_timeout = 500ms;
	config.bootstrap->ondemand.request_timeout = 60s;
	config.bootstrap->ondemand.max_attempts = 2;
	auto & client = *system.add_node (config);

	// The silent node holds the block but will not serve it
	auto const send = make_genesis_send (system, nano::dev::genesis->hash (), nano::dev::constants.genesis_amount - 1, nano::keypair{}.pub);
	ASSERT_EQ (nano::block_status::progress, server.process (send));

	client.bootstrap.demand (send->hash ());

	// Both requests fail on their tags and the block is abandoned, without the strategy's own timeout ever firing
	ASSERT_TIMELY_EQ (10s, 1, client.stats.count (nano::stat::type::bootstrap_ondemand_fail, nano::stat::detail::abandoned));
	ASSERT_EQ (2, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::request));
	ASSERT_EQ (2, client.stats.count (nano::stat::type::bootstrap_timeout, nano::stat::detail::single_block));
	ASSERT_EQ (0, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::timeout));
}

/*
 * A peer that answers with a block other than the requested one is not believed, and the block is requested again.
 * The peer's first response is forged to carry an unrelated block; the second, genuine one delivers the block.
 */
TEST (bootstrap_ondemand, wrong_block_requested_again)
{
	// Declared ahead of the system so the server's observer never outlives it
	std::atomic<bool> forged{ false };

	nano::test::system system;
	auto & server = *system.add_node (server_config (system));
	auto & client = *system.add_node (client_config (system));

	// Only the serving node holds the block
	auto const send = make_genesis_send (system, nano::dev::genesis->hash (), nano::dev::constants.genesis_amount - 1, nano::keypair{}.pub);
	ASSERT_EQ (nano::block_status::progress, server.process (send));

	// Its first response carries some other block instead
	auto const other = make_genesis_send (system, nano::dev::genesis->hash (), nano::dev::constants.genesis_amount - 2, nano::keypair{}.pub);
	forge_first_response (server, client, forged, [other] (auto const &) {
		return std::deque<std::shared_ptr<nano::block>>{ other };
	});

	client.bootstrap.demand (send->hash ());

	// The forged response is rejected as invalid and neither block is taken from it
	ASSERT_TIMELY_EQ (5s, 1, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::invalid));
	ASSERT_EQ (1, client.stats.count (nano::stat::type::bootstrap, nano::stat::detail::invalid_response));

	// The block is requested a second time and arrives
	ASSERT_TIMELY (5s, client.block (send->hash ()) != nullptr);
	ASSERT_EQ (2, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::request));
	ASSERT_EQ (nullptr, client.block (other->hash ()));
}

/*
 * A copy of the requested block with a broken signature is refused by the ledger, and the block is requested again.
 * A block's hash does not cover its signature, so such a copy passes the hash check of the response; only processing it tells.
 */
TEST (bootstrap_ondemand, bad_signature_requested_again)
{
	// Declared ahead of the system so the server's observer never outlives it
	std::atomic<bool> forged{ false };

	nano::test::system system;
	auto & server = *system.add_node (server_config (system));
	auto & client = *system.add_node (client_config (system));

	// Only the serving node holds the block
	auto const send = make_genesis_send (system, nano::dev::genesis->hash (), nano::dev::constants.genesis_amount - 1, nano::keypair{}.pub);
	ASSERT_EQ (nano::block_status::progress, server.process (send));

	// Its first response carries the block with the signature replaced
	forge_first_response (server, client, forged, [] (auto const & blocks) {
		auto const copy = blocks.front ()->clone ();
		copy->signature_set (nano::signature{ 1 });
		return std::deque<std::shared_ptr<nano::block>>{ copy };
	});

	client.bootstrap.demand (send->hash ());

	// The copy has the right hash, so it is taken and handed on, and the ledger refuses it
	ASSERT_TIMELY_EQ (5s, 1, client.stats.count (nano::stat::type::block_processor_result, nano::stat::detail::bad_signature));
	ASSERT_TIMELY_EQ (5s, 1, client.stats.count (nano::stat::type::bootstrap_ondemand_fail, nano::stat::detail::retry));

	// The block is requested a second time and the genuine copy enters the ledger
	ASSERT_TIMELY (5s, client.block (send->hash ()) != nullptr);
	ASSERT_EQ (2, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::request));
	ASSERT_EQ (2, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::fetched));
}

/*
 * A copy of the requested block with insufficient work is turned away by the block processor, and the block is requested again.
 * Like the signature, the work is not covered by the block's hash.
 */
TEST (bootstrap_ondemand, bad_work_requested_again)
{
	// Declared ahead of the system so the server's observer never outlives it
	std::atomic<bool> forged{ false };

	nano::test::system system;
	auto & server = *system.add_node (server_config (system));
	auto & client = *system.add_node (client_config (system));

	// Only the serving node holds the block
	auto const send = make_genesis_send (system, nano::dev::genesis->hash (), nano::dev::constants.genesis_amount - 1, nano::keypair{}.pub);
	ASSERT_EQ (nano::block_status::progress, server.process (send));

	// Its first response carries the block with the work zeroed
	forge_first_response (server, client, forged, [] (auto const & blocks) {
		auto const copy = blocks.front ()->clone ();
		copy->block_work_set (0);
		return std::deque<std::shared_ptr<nano::block>>{ copy };
	});

	client.bootstrap.demand (send->hash ());

	// The copy has the right hash, so it is taken, and the block processor turns it away
	ASSERT_TIMELY_EQ (5s, 1, client.stats.count (nano::stat::type::block_processor, nano::stat::detail::insufficient_work));
	ASSERT_TIMELY_EQ (5s, 1, client.stats.count (nano::stat::type::bootstrap_ondemand_fail, nano::stat::detail::retry));

	// The block is requested a second time and the genuine copy enters the ledger
	ASSERT_TIMELY (5s, client.block (send->hash ()) != nullptr);
	ASSERT_EQ (2, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::request));
	ASSERT_EQ (2, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::fetched));
}

/*
 * Nodes that block flooding never reaches still end up with every block confirmed.
 * A representative creates a chain of blocks and votes on it, but floods none of the blocks, so its two peers only ever see its votes.
 * Each of them fetches the blocks on the strength of those votes and confirms them with the votes it cached, without receiving a single published block.
 */
TEST (bootstrap_ondemand, votes_only_network)
{
	nano::test::system system;

	// The representative votes and serves blocks, and floods nothing
	auto rep_config = server_config (system);
	rep_config.local_block_broadcaster->enable = false;
	auto & rep = *system.add_node (rep_config);
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);

	// Its two peers can only obtain blocks on demand, and cache the representative's votes once they know its tier
	auto & client1 = *system.add_node (client_config (system));
	auto & client2 = *system.add_node (client_config (system));
	ASSERT_TIMELY (5s, client1.rep_tiers.tier (nano::dev::genesis_key.pub) != nano::rep_tier::none);
	ASSERT_TIMELY (5s, client2.rep_tiers.tier (nano::dev::genesis_key.pub) != nano::rep_tier::none);

	// The representative processes a chain of three blocks of its own, which starts its elections and its voting
	std::vector<nano::block_hash> hashes;
	auto previous = nano::dev::genesis->hash ();
	for (auto n = 1; n <= 3; ++n)
	{
		auto const block = make_genesis_send (system, previous, nano::dev::constants.genesis_amount - n, nano::keypair{}.pub);
		ASSERT_EQ (nano::block_status::progress, rep.process_local (block).value ());
		hashes.push_back (block->hash ());
		previous = block->hash ();
	}

	// Both peers confirm the whole chain
	ASSERT_TIMELY (20s, nano::test::confirmed (client1, hashes));
	ASSERT_TIMELY (20s, nano::test::confirmed (client2, hashes));

	// Every block reached them through an on-demand request, none was published to them
	ASSERT_EQ (0, client1.stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::in));
	ASSERT_EQ (0, client2.stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::in));
	ASSERT_GE (client1.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::fetched), 3);
	ASSERT_GE (client2.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::fetched), 3);
}

/*
 * A block fetched as a dependency does not have its own missing source fetched in turn.
 * Anyone can publish a block that names an arbitrary source, so if a block fetched on that ground passed the demand on,
 * a made up block could lead the node through a whole chain of them. Here a live block names a source that itself misses a source.
 */
TEST (bootstrap_ondemand, dependency_does_not_cascade)
{
	nano::test::system system;
	auto & server = *system.add_node (server_config (system));
	auto & client = *system.add_node (client_config (system));

	// On the serving node an account is opened by receiving a send from genesis
	nano::keypair key1;
	auto const send = make_genesis_send (system, nano::dev::genesis->hash (), nano::dev::constants.genesis_amount - 1, key1.pub);
	auto const open = make_open (system, key1, send, 1);
	ASSERT_TRUE (nano::test::process (server, { send, open }));

	// A block that claims to receive from that open block; no ledger would accept it, but that its source is missing is noticed first
	nano::keypair key2;
	auto const bogus = make_open (system, key2, open, 1);

	// It arrives live at a node that holds none of the three
	client.process_active (bogus);

	// The open block is fetched as its source and found to miss a source of its own, the send
	ASSERT_TIMELY_EQ (5s, 1, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::gap_source));

	// The send is not fetched in turn
	ASSERT_ALWAYS_EQ (1s, 1, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::request));
	ASSERT_EQ (1, client.stats.count (nano::stat::type::bootstrap_ondemand_insert, nano::stat::detail::inserted));
	ASSERT_EQ (nullptr, client.block (send->hash ()));
}

/*
 * Requests for blocks that are wanted only as a dependency are held to a budget of their own.
 * With one such request allowed per second the sources of two live blocks cannot both be fetched in under a second,
 * although the strategy's overall rate limit would allow that many times over.
 */
TEST (bootstrap_ondemand, dependency_rate_limit)
{
	nano::test::system system;
	auto & server = *system.add_node (server_config (system));
	auto config = client_config (system);
	config.bootstrap->ondemand.dependency_rate_limit = 1;
	auto & client = *system.add_node (config);

	// Two sends from genesis, each received into a new account, all held by the serving node only
	nano::keypair key1;
	nano::keypair key2;
	auto const send1 = make_genesis_send (system, nano::dev::genesis->hash (), nano::dev::constants.genesis_amount - 1, key1.pub);
	auto const send2 = make_genesis_send (system, send1->hash (), nano::dev::constants.genesis_amount - 2, key2.pub);
	auto const open1 = make_open (system, key1, send1, 1);
	auto const open2 = make_open (system, key2, send2, 1);
	ASSERT_TRUE (nano::test::process (server, { send1, send2, open1, open2 }));

	// Both receiving blocks arrive live without their sends
	auto const start = std::chrono::steady_clock::now ();
	client.process_active (open1);
	client.process_active (open2);

	// Both sends are fetched, the second one only once the budget allows another request
	ASSERT_TIMELY_EQ (10s, 2, client.stats.count (nano::stat::type::bootstrap_ondemand, nano::stat::detail::fetched));
	auto const elapsed = std::chrono::steady_clock::now () - start;
	ASSERT_GE (elapsed, 900ms);

	// With their sends in the ledger the receiving blocks follow
	ASSERT_TIMELY (5s, client.block (open1->hash ()) != nullptr);
	ASSERT_TIMELY (5s, client.block (open2->hash ()) != nullptr);
}
