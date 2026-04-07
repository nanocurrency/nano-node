#include <nano/lib/blocks.hpp>
#include <nano/lib/stats.hpp>
#include <nano/messages/confirm.hpp>
#include <nano/node/network.hpp>
#include <nano/node/transport/fake.hpp>
#include <nano/node/transport/test_channel.hpp>
#include <nano/node/vote_replier.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/test_common/chains.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

/*
 * Requests should be rejected when voting is disabled
 */
TEST (vote_replier, disabled)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.enable_voting = false;
	auto & node = *system.add_node (config);

	auto channel = nano::test::fake_channel (node);
	ASSERT_FALSE (node.vote_replier.request ({ { nano::dev::genesis->hash (), nano::dev::genesis->root () } }, channel));
	ASSERT_TRUE (node.vote_replier.empty ());
	ASSERT_EQ (0, node.stats.count (nano::stat::type::vote_replier, nano::stat::detail::request));
}

/*
 * Requests exceeding confirm_ack_hashes_max should be rejected
 */
TEST (vote_replier, request_too_large)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.enable_voting = true;
	auto & node = *system.add_node (config);

	// Build a request larger than confirm_ack_hashes_max (255)
	nano::vote_replier::request_type request;
	for (uint64_t i = 0; i <= nano::network::confirm_ack_hashes_max; ++i)
	{
		request.emplace_back (nano::block_hash{ i + 1 }, nano::root{ i + 1 });
	}
	ASSERT_EQ (256, request.size ());

	// Request should be rejected
	auto channel = nano::test::fake_channel (node);
	ASSERT_FALSE (node.vote_replier.request (request, channel));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::vote_replier, nano::stat::detail::request));
}

/*
 * Request for a hash/root not in the ledger should not produce a vote
 */
TEST (vote_replier, unknown_block)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.enable_voting = true;
	auto & node = *system.add_node (config);
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);

	auto channel = nano::test::test_channel (node);
	auto future = channel->observe<nano::messages::confirm_ack> ();

	// Request with a hash/root that doesn't exist in the ledger
	ASSERT_TRUE (node.vote_replier.request ({ { nano::block_hash{ 42 }, nano::root{ 42 } } }, channel));

	ASSERT_TIMELY_EQ (5s, node.stats.count (nano::stat::type::vote_replier, nano::stat::detail::reply_unknown), 1);
	ASSERT_EQ (0, node.stats.count (nano::stat::type::vote_replier, nano::stat::detail::reply_final));

	// No vote should be sent since there are no permits
	ASSERT_NE (future.wait_for (0s), std::future_status::ready);
}

/*
 * Request for an unconfirmed block should be skipped by voting policy
 */
TEST (vote_replier, block_not_eligible)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.enable_voting = true;
	config.backlog_scan.enable = false;
	config.hinted_scheduler.enable = false;
	config.optimistic_scheduler.enable = false;
	auto & node = *system.add_node (config);
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);

	// Create a block but don't confirm it - voting policy will reject
	auto blocks = nano::test::setup_chain (system, node, 1, nano::dev::genesis_key, false /* don't confirm */);

	auto channel = nano::test::test_channel (node);
	auto future = channel->observe<nano::messages::confirm_ack> ();

	ASSERT_TRUE (node.vote_replier.request ({ { blocks[0]->hash (), blocks[0]->root () } }, channel));

	ASSERT_TIMELY_EQ (5s, node.stats.count (nano::stat::type::vote_replier, nano::stat::detail::reply_skip), 1);
	ASSERT_EQ (0, node.stats.count (nano::stat::type::vote_replier, nano::stat::detail::reply_final));

	// No vote should be sent
	ASSERT_NE (future.wait_for (0s), std::future_status::ready);
}

/*
 * Request for a single confirmed block should produce a final vote reply
 */
TEST (vote_replier, single_hash_reply)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.enable_voting = true;
	auto & node = *system.add_node (config);
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	node.wallets.refresh_reps ();

	auto blocks = nano::test::setup_chain (system, node, 1);

	auto channel = nano::test::test_channel (node);
	auto future = channel->observe<nano::messages::confirm_ack> ();

	ASSERT_TRUE (node.vote_replier.request ({ { blocks[0]->hash (), blocks[0]->root () } }, channel));

	ASSERT_EQ (future.wait_for (5s), std::future_status::ready);
	auto ack = future.get ();

	ASSERT_TRUE (ack.vote->is_final ());
	ASSERT_EQ (1, ack.vote->hashes.size ());
	ASSERT_EQ (blocks[0]->hash (), ack.vote->hashes[0]);
	ASSERT_EQ (nano::dev::genesis_key.pub, ack.vote->account);
}

/*
 * Multiple hashes in a single request should be batched into one vote
 */
TEST (vote_replier, multiple_hashes_reply)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.enable_voting = true;
	auto & node = *system.add_node (config);
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	node.wallets.refresh_reps ();

	auto blocks = nano::test::setup_chain (system, node, 3);

	auto channel = nano::test::test_channel (node);
	auto future = channel->observe<nano::messages::confirm_ack> ();

	// Single request with all 3 hashes
	nano::vote_replier::request_type request;
	for (auto const & block : blocks)
	{
		request.emplace_back (block->hash (), block->root ());
	}
	ASSERT_TRUE (node.vote_replier.request (request, channel));

	// With 1 representative, all 3 hashes should be in a single vote / single confirm_ack
	ASSERT_EQ (future.wait_for (5s), std::future_status::ready);
	auto ack = future.get ();

	ASSERT_TRUE (ack.vote->is_final ());
	ASSERT_EQ (3, ack.vote->hashes.size ());
	for (auto const & block : blocks)
	{
		ASSERT_TRUE (std::find (ack.vote->hashes.begin (), ack.vote->hashes.end (), block->hash ()) != ack.vote->hashes.end ());
	}
}

/*
 * Request mixing known and unknown hashes should only vote for the known ones
 */
TEST (vote_replier, mixed_known_unknown)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.enable_voting = true;
	auto & node = *system.add_node (config);
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	node.wallets.refresh_reps ();

	auto blocks = nano::test::setup_chain (system, node, 1);

	auto channel = nano::test::test_channel (node);
	auto future = channel->observe<nano::messages::confirm_ack> ();

	// Request with one valid hash and one nonexistent hash
	nano::vote_replier::request_type request;
	request.emplace_back (blocks[0]->hash (), blocks[0]->root ());
	request.emplace_back (nano::block_hash{ 999 }, nano::root{ 999 });
	ASSERT_TRUE (node.vote_replier.request (request, channel));

	ASSERT_TIMELY_EQ (5s, node.stats.count (nano::stat::type::vote_replier, nano::stat::detail::reply_unknown), 1);

	// One confirm_ack with only the known hash
	ASSERT_EQ (future.wait_for (5s), std::future_status::ready);
	auto ack = future.get ();

	ASSERT_EQ (1, ack.vote->hashes.size ());
	ASSERT_EQ (blocks[0]->hash (), ack.vote->hashes[0]);
}

/*
 * Requests exceeding per-channel queue limit should be rejected with overfill
 */
TEST (vote_replier, overfill)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.enable_voting = true;
	config.vote_replier.channel_limit = 1;
	auto & node = *system.add_node (config);

	auto channel = nano::test::fake_channel (node);

	// Tight push loop to race against worker threads and trigger at least one overfill
	// With channel_limit=1, we need to push two items between worker dequeues
	auto deadline = std::chrono::steady_clock::now () + 5s;
	while (node.stats.count (nano::stat::type::vote_replier, nano::stat::detail::overfill) < 1)
	{
		ASSERT_LT (std::chrono::steady_clock::now (), deadline);
		for (int i = 0; i < 100; ++i)
		{
			node.vote_replier.request ({ { nano::block_hash{ 1 }, nano::root{ 1 } } }, channel);
		}
	}

	ASSERT_TRUE (node.stats.count (nano::stat::type::vote_replier, nano::stat::detail::request) >= 1);
	ASSERT_TRUE (node.stats.count (nano::stat::type::vote_replier, nano::stat::detail::overfill_hashes) >= 1);

	// Queue should eventually drain
	ASSERT_TIMELY (5s, node.vote_replier.empty ());
}

/*
 * Requests from different channels should both be served via fair queuing
 */
TEST (vote_replier, per_channel_fairness)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.enable_voting = true;
	config.vote_replier.channel_limit = 1;
	auto & node = *system.add_node (config);
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	node.wallets.refresh_reps ();

	auto blocks = nano::test::setup_chain (system, node, 1);

	// Two channels with distinct node_ids so they map to different origins in the fair_queue
	auto channel_a = nano::test::test_channel (node, nano::account{ 1 });
	auto channel_b = nano::test::test_channel (node, nano::account{ 2 });
	auto future_a = channel_a->observe<nano::messages::confirm_ack> ();
	auto future_b = channel_b->observe<nano::messages::confirm_ack> ();

	nano::vote_replier::request_type request = { { blocks[0]->hash (), blocks[0]->root () } };

	ASSERT_TRUE (node.vote_replier.request (request, channel_a));
	ASSERT_TRUE (node.vote_replier.request (request, channel_b));

	// Both channels should receive a confirm_ack reply
	ASSERT_EQ (future_a.wait_for (5s), std::future_status::ready);
	ASSERT_EQ (future_b.wait_for (5s), std::future_status::ready);
}

/*
 * End-to-end: inbound confirm_req triggers vote_replier and sends confirm_ack back on the same channel
 */
TEST (vote_replier, integration_confirm_req)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.enable_voting = true;
	config.backlog_scan.enable = false;
	config.hinted_scheduler.enable = false;
	config.optimistic_scheduler.enable = false;
	auto & node = *system.add_node (config);
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	node.wallets.refresh_reps ();

	// Create and confirm a block
	nano::keypair key;
	auto send = nano::state_block_builder ()
				.account (nano::dev::genesis_key.pub)
				.previous (nano::dev::genesis->hash ())
				.representative (nano::dev::genesis_key.pub)
				.balance (nano::dev::constants.genesis_amount - nano::nano_ratio)
				.link (key.pub)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*system.work.generate (nano::dev::genesis->hash ()))
				.build ();
	ASSERT_EQ (nano::block_status::progress, node.process (send));
	nano::test::confirm (node.ledger, send);

	// Simulate an incoming confirm_req message via inbound
	auto channel = nano::test::test_channel (node);
	auto future = channel->observe<nano::messages::confirm_ack> ();

	nano::messages::confirm_req message{ nano::dev::network_params.network, send->hash (), send->root () };
	node.inbound (message, channel);

	// Verify the vote_replier processed the request and generated a reply
	ASSERT_TIMELY_EQ (5s, node.stats.count (nano::stat::type::vote_replier, nano::stat::detail::reply_final), 1);
	ASSERT_TIMELY_EQ (5s, node.stats.count (nano::stat::type::vote_replier, nano::stat::detail::reply_hashes), 1);

	// Verify the reply is sent back on the same channel
	ASSERT_EQ (future.wait_for (5s), std::future_status::ready);
	auto ack = future.get ();

	ASSERT_TRUE (ack.vote->is_final ());
	ASSERT_EQ (1, ack.vote->hashes.size ());
	ASSERT_EQ (send->hash (), ack.vote->hashes[0]);
	ASSERT_EQ (nano::dev::genesis_key.pub, ack.vote->account);
}

/*
 * Request for a forked open block should return vote for the correct fork alternative
 */
TEST (vote_replier, forked_open)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);

	// Setup two forks of the open block
	nano::keypair key;
	nano::block_builder builder;
	auto send0 = builder.send ()
				 .previous (nano::dev::genesis->hash ())
				 .destination (key.pub)
				 .balance (nano::dev::constants.genesis_amount - 500)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();
	auto open0 = builder.open ()
				 .source (send0->hash ())
				 .representative (1)
				 .account (key.pub)
				 .sign (key.prv, key.pub)
				 .work (*system.work.generate (key.pub))
				 .build ();
	auto open1 = builder.open ()
				 .source (send0->hash ())
				 .representative (2)
				 .account (key.pub)
				 .sign (key.prv, key.pub)
				 .work (*system.work.generate (key.pub))
				 .build ();

	nano::test::process (node, { send0, open0 });
	nano::test::confirm (node, { open0 });

	auto channel = nano::test::test_channel (node);
	auto future = channel->observe<nano::messages::confirm_ack> ();

	// Request vote for the wrong fork
	ASSERT_TRUE (node.vote_replier.request ({ { open1->hash (), open1->root () } }, channel));

	ASSERT_EQ (future.wait_for (5s), std::future_status::ready);
	auto ack = future.get ();

	ASSERT_EQ (1, ack.vote->hashes.size ());
	ASSERT_EQ (open0->hash (), ack.vote->hashes[0]); // Vote for the correct fork alternative
	ASSERT_EQ (nano::dev::genesis_key.pub, ack.vote->account);
}

/*
 * Request for a conflicting epoch block should return vote for the correct alternative
 */
TEST (vote_replier, epoch_conflict)
{
	nano::test::system system;

	nano::node_flags node_flags;
	node_flags.disable_rep_crawler = true;
	auto & node = *system.add_node (node_flags);
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);

	nano::keypair key;
	nano::keypair epoch_signer (nano::dev::genesis_key);
	nano::state_block_builder builder;

	auto send = builder.make_block ()
				.account (nano::dev::genesis_key.pub)
				.previous (nano::dev::genesis->hash ())
				.representative (nano::dev::genesis_key.pub)
				.balance (nano::dev::constants.genesis_amount - 1)
				.link (key.pub)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*system.work.generate (nano::dev::genesis->hash ()))
				.build ();

	auto open = builder.make_block ()
				.account (key.pub)
				.previous (0)
				.representative (key.pub)
				.balance (1)
				.link (send->hash ())
				.sign (key.prv, key.pub)
				.work (*system.work.generate (key.pub))
				.build ();

	// Change block root is the open block hash, qualified root: {open, open}
	auto change = builder.make_block ()
				  .account (key.pub)
				  .previous (open->hash ())
				  .representative (key.pub)
				  .balance (1)
				  .link (0)
				  .sign (key.prv, key.pub)
				  .work (*system.work.generate (open->hash ()))
				  .build ();

	// Pending entry is needed first to process the epoch open block
	auto pending = builder.make_block ()
				   .account (nano::dev::genesis_key.pub)
				   .previous (send->hash ())
				   .representative (nano::dev::genesis_key.pub)
				   .balance (nano::dev::constants.genesis_amount - 2)
				   .link (change->root ().as_account ())
				   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				   .work (*system.work.generate (send->hash ()))
				   .build ();

	// Create conflicting epoch block with the same root as the change block, qualified root: {open, 0}
	auto epoch_open = builder.make_block ()
					  .account (change->root ().as_account ())
					  .previous (0)
					  .representative (0)
					  .balance (0)
					  .link (node.ledger.epoch_link (nano::epoch::epoch_1))
					  .sign (epoch_signer.prv, epoch_signer.pub)
					  .work (*system.work.generate (open->hash ()))
					  .build ();

	// Process and confirm the initial chain with the change block
	nano::test::process (node, { send, open, change });
	nano::test::confirm (node, { change });
	ASSERT_TIMELY (5s, node.block_confirmed (change->hash ()));

	auto channel = nano::test::test_channel (node);

	// Request vote for the conflicting epoch block
	auto future1 = channel->observe<nano::messages::confirm_ack> ();
	ASSERT_TRUE (node.vote_replier.request ({ { epoch_open->hash (), epoch_open->root () } }, channel));

	ASSERT_EQ (future1.wait_for (5s), std::future_status::ready);
	auto ack1 = future1.get ();

	ASSERT_EQ (1, ack1.vote->hashes.size ());
	ASSERT_EQ (change->hash (), ack1.vote->hashes[0]); // Vote for the correct alternative (change block)
	ASSERT_EQ (nano::dev::genesis_key.pub, ack1.vote->account);

	// Process the conflicting epoch block
	nano::test::process (node, { pending, epoch_open });
	nano::test::confirm (node, { pending, epoch_open });

	// Workaround for vote spacing dropping requests with the same root
	// FIXME: Vote spacing should use full qualified root
	WAIT (1s);

	// Request vote for the conflicting epoch block again
	auto future2 = channel->observe<nano::messages::confirm_ack> ();
	ASSERT_TRUE (node.vote_replier.request ({ { epoch_open->hash (), epoch_open->root () } }, channel));

	ASSERT_EQ (future2.wait_for (5s), std::future_status::ready);
	auto ack2 = future2.get ();

	ASSERT_EQ (1, ack2.vote->hashes.size ());
	ASSERT_EQ (epoch_open->hash (), ack2.vote->hashes[0]); // Vote for the epoch block
	ASSERT_EQ (nano::dev::genesis_key.pub, ack2.vote->account);
}

/*
 * Request for multiple cemented blocks in a chain should generate votes regardless of vote spacing
 */
TEST (vote_replier, cemented_no_spacing)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);

	auto blocks = nano::test::setup_chain (system, node, 3);

	auto channel = nano::test::test_channel (node);
	auto future = channel->observe<nano::messages::confirm_ack> ();

	nano::vote_replier::request_type request;
	for (auto const & block : blocks)
	{
		request.emplace_back (block->hash (), block->root ());
	}
	ASSERT_TRUE (node.vote_replier.request (request, channel));

	ASSERT_EQ (future.wait_for (5s), std::future_status::ready);
	auto ack = future.get ();

	ASSERT_EQ (3, ack.vote->hashes.size ());
	ASSERT_EQ (nano::dev::genesis_key.pub, ack.vote->account);

	for (auto const & block : blocks)
	{
		ASSERT_TRUE (std::find (ack.vote->hashes.begin (), ack.vote->hashes.end (), block->hash ()) != ack.vote->hashes.end ());
	}
}

/*
 * Node with two representatives should produce two separate votes for the same request
 */
TEST (vote_replier, multiple_representatives)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	auto & wallet = *system.wallet (0);

	// Set up two representatives with voting weight
	nano::keypair rep2;
	wallet.insert_adhoc (nano::dev::genesis_key.prv);
	wallet.insert_adhoc (rep2.prv);

	auto const amount = 100 * nano::Knano_ratio;
	wallet.send_sync (nano::dev::genesis_key.pub, rep2.pub, amount);
	ASSERT_TIMELY (5s, node.balance (rep2.pub) == amount);
	wallet.change_sync (rep2.pub, rep2.pub);
	ASSERT_EQ (node.weight (rep2.pub), amount);
	node.wallets.refresh_reps ();
	ASSERT_EQ (2, node.wallets.reps ().voting);

	auto blocks = nano::test::setup_chain (system, node, 1);

	auto channel = nano::test::test_channel (node);

	// Collect all confirm_ack messages
	std::vector<std::shared_ptr<nano::vote>> votes;
	nano::mutex votes_mutex;
	channel->observe ([&] (nano::messages::message const & message, nano::transport::traffic_type) {
		if (auto * ack = dynamic_cast<nano::messages::confirm_ack const *> (&message))
		{
			nano::lock_guard<nano::mutex> guard{ votes_mutex };
			votes.push_back (ack->vote);
		}
	});

	ASSERT_TRUE (node.vote_replier.request ({ { blocks[0]->hash (), blocks[0]->root () } }, channel));

	// Two representatives should produce two separate votes
	ASSERT_TIMELY_EQ (5s, node.stats.count (nano::stat::type::vote_replier, nano::stat::detail::reply_final), 1);
	auto get_votes_size = [&] () {
		nano::lock_guard<nano::mutex> guard{ votes_mutex };
		return votes.size ();
	};
	ASSERT_TIMELY_EQ (5s, get_votes_size (), 2);

	nano::lock_guard<nano::mutex> guard{ votes_mutex };
	std::set<nano::account> signers;
	for (auto const & vote : votes)
	{
		ASSERT_TRUE (vote->is_final ());
		ASSERT_EQ (1, vote->hashes.size ());
		ASSERT_EQ (blocks[0]->hash (), vote->hashes[0]);
		signers.insert (vote->account);
	}

	// Both representatives should have signed
	ASSERT_TRUE (signers.count (nano::dev::genesis_key.pub));
	ASSERT_TRUE (signers.count (rep2.pub));
}
