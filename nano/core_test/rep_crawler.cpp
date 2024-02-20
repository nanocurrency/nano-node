#include <nano/lib/config.hpp>
#include <nano/lib/logging.hpp>
#include <nano/node/repcrawler.hpp>
#include <nano/node/transport/fake.hpp>
#include <nano/node/transport/inproc.hpp>
#include <nano/test_common/network.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <numeric>

using namespace std::chrono_literals;

// Test that nodes can track nodes that have rep weight for priority broadcasting
TEST (rep_crawler, rep_list)
{
	nano::test::system system;
	auto & node1 = *system.add_node ();
	auto & node2 = *system.add_node ();
	ASSERT_EQ (0, node2.rep_crawler.representative_count ());
	// Node #1 has a rep
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	ASSERT_TIMELY_EQ (5s, node2.rep_crawler.representative_count (), 1);
	auto reps = node2.rep_crawler.representatives ();
	ASSERT_EQ (1, reps.size ());
	ASSERT_EQ (nano::dev::genesis_key.pub, reps[0].account);
}

TEST (rep_crawler, rep_weight)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	auto & node1 = *system.add_node ();
	auto & node2 = *system.add_node ();
	auto & node3 = *system.add_node ();
	nano::keypair keypair1;
	nano::keypair keypair2;
	nano::block_builder builder;
	auto const amount_pr = node.minimum_principal_weight () + 100;
	auto const amount_not_pr = node.minimum_principal_weight () - 100;
	std::shared_ptr<nano::block> block1 = builder
										  .state ()
										  .account (nano::dev::genesis_key.pub)
										  .previous (nano::dev::genesis->hash ())
										  .representative (nano::dev::genesis_key.pub)
										  .balance (nano::dev::constants.genesis_amount - amount_not_pr)
										  .link (keypair1.pub)
										  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
										  .work (*system.work.generate (nano::dev::genesis->hash ()))
										  .build ();
	std::shared_ptr<nano::block> block2 = builder
										  .state ()
										  .account (keypair1.pub)
										  .previous (0)
										  .representative (keypair1.pub)
										  .balance (amount_not_pr)
										  .link (block1->hash ())
										  .sign (keypair1.prv, keypair1.pub)
										  .work (*system.work.generate (keypair1.pub))
										  .build ();
	std::shared_ptr<nano::block> block3 = builder
										  .state ()
										  .account (nano::dev::genesis_key.pub)
										  .previous (block1->hash ())
										  .representative (nano::dev::genesis_key.pub)
										  .balance (nano::dev::constants.genesis_amount - amount_not_pr - amount_pr)
										  .link (keypair2.pub)
										  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
										  .work (*system.work.generate (block1->hash ()))
										  .build ();
	std::shared_ptr<nano::block> block4 = builder
										  .state ()
										  .account (keypair2.pub)
										  .previous (0)
										  .representative (keypair2.pub)
										  .balance (amount_pr)
										  .link (block3->hash ())
										  .sign (keypair2.prv, keypair2.pub)
										  .work (*system.work.generate (keypair2.pub))
										  .build ();
	ASSERT_TRUE (nano::test::process (node, { block1, block2, block3, block4 }));
	ASSERT_TRUE (nano::test::process (node1, { block1, block2, block3, block4 }));
	ASSERT_TRUE (nano::test::process (node2, { block1, block2, block3, block4 }));
	ASSERT_TRUE (nano::test::process (node3, { block1, block2, block3, block4 }));
	ASSERT_TRUE (node.rep_crawler.representatives (1).empty ());
	std::shared_ptr<nano::transport::channel> channel1 = nano::test::establish_tcp (system, node, node1.network.endpoint ());
	ASSERT_NE (nullptr, channel1);
	std::shared_ptr<nano::transport::channel> channel2 = nano::test::establish_tcp (system, node, node2.network.endpoint ());
	ASSERT_NE (nullptr, channel2);
	std::shared_ptr<nano::transport::channel> channel3 = nano::test::establish_tcp (system, node, node3.network.endpoint ());
	ASSERT_NE (nullptr, channel3);
	auto vote0 = std::make_shared<nano::vote> (nano::dev::genesis_key.pub, nano::dev::genesis_key.prv, 0, 0, std::vector<nano::block_hash>{ nano::dev::genesis->hash () });
	auto vote1 = std::make_shared<nano::vote> (keypair1.pub, keypair1.prv, 0, 0, std::vector<nano::block_hash>{ nano::dev::genesis->hash () });
	auto vote2 = std::make_shared<nano::vote> (keypair2.pub, keypair2.prv, 0, 0, std::vector<nano::block_hash>{ nano::dev::genesis->hash () });
	ASSERT_TRUE (node.rep_crawler.process (vote0, channel1));
	ASSERT_TRUE (node.rep_crawler.process (vote1, channel2));
	ASSERT_TRUE (node.rep_crawler.process (vote2, channel3));
	ASSERT_TIMELY_EQ (5s, node.rep_crawler.representative_count (), 2);
	// Make sure we get the rep with the most weight first
	auto reps = node.rep_crawler.representatives (1);
	ASSERT_EQ (1, reps.size ());
	ASSERT_EQ (node.balance (nano::dev::genesis_key.pub), node.ledger.weight (reps[0].account));
	ASSERT_EQ (nano::dev::genesis_key.pub, reps[0].account);
	ASSERT_EQ (*channel1, *reps[0].channel);
	ASSERT_TRUE (node.rep_crawler.is_pr (channel1));
	ASSERT_FALSE (node.rep_crawler.is_pr (channel2));
	ASSERT_TRUE (node.rep_crawler.is_pr (channel3));
}

// Test that rep_crawler removes unreachable reps from its search results.
// This test creates three principal representatives (rep1, rep2, genesis_rep) and
// one node for searching them (searching_node).
TEST (rep_crawler, rep_remove)
{
	nano::test::system system;
	auto & searching_node = *system.add_node (); // will be used to find principal representatives
	nano::keypair keys_rep1; // Principal representative 1
	nano::keypair keys_rep2; // Principal representative 2
	nano::block_builder builder;

	// Send enough nanos to Rep1 to make it a principal representative
	std::shared_ptr<nano::block> send_to_rep1 = builder
												.state ()
												.account (nano::dev::genesis_key.pub)
												.previous (nano::dev::genesis->hash ())
												.representative (nano::dev::genesis_key.pub)
												.balance (nano::dev::constants.genesis_amount - searching_node.minimum_principal_weight () * 2)
												.link (keys_rep1.pub)
												.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
												.work (*system.work.generate (nano::dev::genesis->hash ()))
												.build ();

	// Receive by Rep1
	std::shared_ptr<nano::block> receive_rep1 = builder
												.state ()
												.account (keys_rep1.pub)
												.previous (0)
												.representative (keys_rep1.pub)
												.balance (searching_node.minimum_principal_weight () * 2)
												.link (send_to_rep1->hash ())
												.sign (keys_rep1.prv, keys_rep1.pub)
												.work (*system.work.generate (keys_rep1.pub))
												.build ();

	// Send enough nanos to Rep2 to make it a principal representative
	std::shared_ptr<nano::block> send_to_rep2 = builder
												.state ()
												.account (nano::dev::genesis_key.pub)
												.previous (send_to_rep1->hash ())
												.representative (nano::dev::genesis_key.pub)
												.balance (nano::dev::constants.genesis_amount - searching_node.minimum_principal_weight () * 4)
												.link (keys_rep2.pub)
												.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
												.work (*system.work.generate (send_to_rep1->hash ()))
												.build ();

	// Receive by Rep2
	std::shared_ptr<nano::block> receive_rep2 = builder
												.state ()
												.account (keys_rep2.pub)
												.previous (0)
												.representative (keys_rep2.pub)
												.balance (searching_node.minimum_principal_weight () * 2)
												.link (send_to_rep2->hash ())
												.sign (keys_rep2.prv, keys_rep2.pub)
												.work (*system.work.generate (keys_rep2.pub))
												.build ();
	{
		auto transaction = searching_node.store.tx_begin_write ();
		ASSERT_EQ (nano::block_status::progress, searching_node.ledger.process (transaction, send_to_rep1));
		ASSERT_EQ (nano::block_status::progress, searching_node.ledger.process (transaction, receive_rep1));
		ASSERT_EQ (nano::block_status::progress, searching_node.ledger.process (transaction, send_to_rep2));
		ASSERT_EQ (nano::block_status::progress, searching_node.ledger.process (transaction, receive_rep2));
	}

	// Create channel for Rep1
	auto channel_rep1 (std::make_shared<nano::transport::fake::channel> (searching_node));

	// Ensure Rep1 is found by the rep_crawler after receiving a vote from it
	auto vote_rep1 = std::make_shared<nano::vote> (keys_rep1.pub, keys_rep1.prv, 0, 0, std::vector<nano::block_hash>{ nano::dev::genesis->hash () });
	searching_node.rep_crawler.force_process (vote_rep1, channel_rep1);
	ASSERT_TIMELY_EQ (5s, searching_node.rep_crawler.representative_count (), 1);
	auto reps (searching_node.rep_crawler.representatives (1));
	ASSERT_EQ (1, reps.size ());
	ASSERT_EQ (searching_node.minimum_principal_weight () * 2, searching_node.ledger.weight (reps[0].account));
	ASSERT_EQ (keys_rep1.pub, reps[0].account);
	ASSERT_EQ (*channel_rep1, *reps[0].channel);

	// When rep1 disconnects then rep1 should not be found anymore
	channel_rep1->close ();
	ASSERT_TIMELY_EQ (5s, searching_node.rep_crawler.representative_count (), 0);

	// Add working node for genesis representative
	auto node_genesis_rep = system.add_node (nano::node_config (system.get_available_port ()));
	system.wallet (1)->insert_adhoc (nano::dev::genesis_key.prv);
	auto channel_genesis_rep (searching_node.network.find_node_id (node_genesis_rep->get_node_id ()));
	ASSERT_NE (nullptr, channel_genesis_rep);

	// genesis_rep should be found as principal representative after receiving a vote from it
	auto vote_genesis_rep = std::make_shared<nano::vote> (nano::dev::genesis_key.pub, nano::dev::genesis_key.prv, 0, 0, std::vector<nano::block_hash>{ nano::dev::genesis->hash () });
	searching_node.rep_crawler.force_process (vote_genesis_rep, channel_genesis_rep);
	ASSERT_TIMELY_EQ (10s, searching_node.rep_crawler.representative_count (), 1);

	// Start a node for Rep2 and wait until it is connected
	auto node_rep2 (std::make_shared<nano::node> (system.io_ctx, nano::unique_path (), nano::node_config (system.get_available_port ()), system.work));
	node_rep2->start ();
	searching_node.network.tcp_channels.start_tcp (node_rep2->network.endpoint ());
	std::shared_ptr<nano::transport::channel> channel_rep2;
	ASSERT_TIMELY (10s, (channel_rep2 = searching_node.network.tcp_channels.find_node_id (node_rep2->get_node_id ())) != nullptr);

	// Rep2 should be found as a principal representative after receiving a vote from it
	auto vote_rep2 = std::make_shared<nano::vote> (keys_rep2.pub, keys_rep2.prv, 0, 0, std::vector<nano::block_hash>{ nano::dev::genesis->hash () });
	searching_node.rep_crawler.force_process (vote_rep2, channel_rep2);
	ASSERT_TIMELY_EQ (10s, searching_node.rep_crawler.representative_count (), 2);

	// When Rep2 is stopped, it should not be found as principal representative anymore
	node_rep2->stop ();
	ASSERT_TIMELY_EQ (10s, searching_node.rep_crawler.representative_count (), 1);

	// Now only genesisRep should be found:
	reps = searching_node.rep_crawler.representatives (1);
	ASSERT_EQ (nano::dev::genesis_key.pub, reps[0].account);
	ASSERT_TIMELY_EQ (5s, searching_node.network.size (), 1);
	auto list (searching_node.network.list (1));
	ASSERT_EQ (node_genesis_rep->network.endpoint (), list[0]->get_endpoint ());
}

TEST (rep_crawler, rep_connection_close)
{
	nano::test::system system;
	auto & node1 = *system.add_node ();
	auto & node2 = *system.add_node ();
	// Add working representative (node 2)
	system.wallet (1)->insert_adhoc (nano::dev::genesis_key.prv);
	ASSERT_TIMELY_EQ (10s, node1.rep_crawler.representative_count (), 1);
	node2.stop ();
	// Remove representative with closed channel
	ASSERT_TIMELY_EQ (10s, node1.rep_crawler.representative_count (), 0);
}

// This test checks that if a block is in the recently_confirmed list then the repcrawler will not send a request for it.
// The behaviour of this test previously was the opposite, that the repcrawler eventually send out such a block and deleted the block
// from the recently confirmed list to try to make ammends for sending it, which is bad behaviour.
// In the long term, we should have a better way to check for reps and this test should become redundant
TEST (rep_crawler, recently_confirmed)
{
	nano::test::system system (1);
	auto & node1 (*system.nodes[0]);
	ASSERT_EQ (1, node1.ledger.cache.block_count);
	auto const block = nano::dev::genesis;
	node1.active.recently_confirmed.put (block->qualified_root (), block->hash ());
	auto & node2 (*system.add_node ());
	system.wallet (1)->insert_adhoc (nano::dev::genesis_key.prv);
	auto channel = node1.network.find_node_id (node2.get_node_id ());
	ASSERT_NE (nullptr, channel);
	node1.rep_crawler.query (channel); // this query should be dropped due to the recently_confirmed entry
	ASSERT_ALWAYS_EQ (0.5s, node1.rep_crawler.representative_count (), 0);
}

// Votes from local channels should be ignored
TEST (rep_crawler, ignore_local)
{
	nano::test::system system;
	nano::node_flags flags;
	auto & node = *system.add_node (flags);
	auto loopback = std::make_shared<nano::transport::inproc::channel> (node, node);
	auto vote = std::make_shared<nano::vote> (nano::dev::genesis_key.pub, nano::dev::genesis_key.prv, 0, 0, std::vector{ nano::dev::genesis->hash () });
	node.rep_crawler.force_process (vote, loopback);
	ASSERT_ALWAYS_EQ (0.5s, node.rep_crawler.representative_count (), 0);
}