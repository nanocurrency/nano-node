#include <nano/store/block.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (gap_cache, add_new)
{
	nano::test::system system (1);
	nano::gap_cache cache (*system.nodes[0]);
	nano::block_builder builder;
	auto block1 = builder
				  .send ()
				  .previous (0)
				  .destination (1)
				  .balance (2)
				  .sign (nano::keypair ().prv, 4)
				  .work (5)
				  .build_shared ();
	cache.add (block1->hash ());
}

TEST (gap_cache, add_existing)
{
	nano::test::system system (1);
	nano::gap_cache cache (*system.nodes[0]);
	nano::block_builder builder;
	auto block1 = builder
				  .send ()
				  .previous (0)
				  .destination (1)
				  .balance (2)
				  .sign (nano::keypair ().prv, 4)
				  .work (5)
				  .build_shared ();
	cache.add (block1->hash ());
	nano::unique_lock<nano::mutex> lock{ cache.mutex };
	auto existing1 (cache.blocks.get<1> ().find (block1->hash ()));
	ASSERT_NE (cache.blocks.get<1> ().end (), existing1);
	auto arrival (existing1->arrival);
	lock.unlock ();
	ASSERT_TIMELY (20s, arrival != std::chrono::steady_clock::now ());
	cache.add (block1->hash ());
	ASSERT_EQ (1, cache.size ());
	lock.lock ();
	auto existing2 (cache.blocks.get<1> ().find (block1->hash ()));
	ASSERT_NE (cache.blocks.get<1> ().end (), existing2);
	ASSERT_GT (existing2->arrival, arrival);
}

TEST (gap_cache, comparison)
{
	nano::test::system system (1);
	nano::gap_cache cache (*system.nodes[0]);
	nano::block_builder builder;
	auto block1 = builder
				  .send ()
				  .previous (1)
				  .destination (0)
				  .balance (2)
				  .sign (nano::keypair ().prv, 4)
				  .work (5)
				  .build_shared ();
	cache.add (block1->hash ());
	nano::unique_lock<nano::mutex> lock{ cache.mutex };
	auto existing1 (cache.blocks.get<1> ().find (block1->hash ()));
	ASSERT_NE (cache.blocks.get<1> ().end (), existing1);
	auto arrival (existing1->arrival);
	lock.unlock ();
	ASSERT_TIMELY (20s, std::chrono::steady_clock::now () != arrival);
	auto block3 = builder
				  .send ()
				  .previous (0)
				  .destination (42)
				  .balance (1)
				  .sign (nano::keypair ().prv, 3)
				  .work (4)
				  .build_shared ();
	cache.add (block3->hash ());
	ASSERT_EQ (2, cache.size ());
	lock.lock ();
	auto existing2 (cache.blocks.get<1> ().find (block3->hash ()));
	ASSERT_NE (cache.blocks.get<1> ().end (), existing2);
	ASSERT_GT (existing2->arrival, arrival);
	ASSERT_EQ (arrival, cache.blocks.get<1> ().begin ()->arrival);
}

// Upon receiving enough votes for a gapped block, a lazy bootstrap should be initiated
TEST (gap_cache, gap_bootstrap)
{
	nano::node_flags node_flags;
	node_flags.disable_legacy_bootstrap = true;
	node_flags.disable_request_loop = true; // to avoid fallback behavior of broadcasting blocks
	nano::test::system system (2, nano::transport::transport_type::tcp, node_flags);

	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	nano::block_hash latest (node1.latest (nano::dev::genesis_key.pub));
	nano::keypair key;
	nano::block_builder builder;
	auto send = builder
				.send ()
				.previous (latest)
				.destination (key.pub)
				.balance (nano::dev::constants.genesis_amount - 100)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*system.work.generate (latest))
				.build_shared ();
	ASSERT_EQ (nano::process_result::progress, node1.process (*send).code);
	ASSERT_EQ (nano::dev::constants.genesis_amount - 100, node1.balance (nano::dev::genesis->account ()));
	ASSERT_EQ (nano::dev::constants.genesis_amount, node2.balance (nano::dev::genesis->account ()));
	// Confirm send block, allowing voting on the upcoming block
	auto election = nano::test::start_election (system, node1, send->hash ());
	ASSERT_NE (nullptr, election);
	election->force_confirm ();
	ASSERT_TIMELY (5s, node1.block_confirmed (send->hash ()));
	node1.active.erase (*send);
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	auto latest_block (system.wallet (0)->send_action (nano::dev::genesis_key.pub, key.pub, 100));
	ASSERT_NE (nullptr, latest_block);
	ASSERT_TIMELY_EQ (5s, nano::dev::constants.genesis_amount - 200, node1.balance (nano::dev::genesis->account ()));
	ASSERT_TIMELY_EQ (5s, nano::dev::constants.genesis_amount, node2.balance (nano::dev::genesis->account ()));
	ASSERT_TIMELY_EQ (5s, node2.balance (nano::dev::genesis->account ()), nano::dev::constants.genesis_amount - 200);
}

TEST (gap_cache, two_dependencies)
{
	nano::test::system system (1);
	auto & node1 (*system.nodes[0]);
	nano::keypair key;
	nano::block_builder builder;
	auto send1 = builder
				 .send ()
				 .previous (nano::dev::genesis->hash ())
				 .destination (key.pub)
				 .balance (1)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build_shared ();
	auto send2 = builder
				 .send ()
				 .previous (send1->hash ())
				 .destination (key.pub)
				 .balance (0)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build_shared ();
	auto open = builder
				.open ()
				.source (send1->hash ())
				.representative (key.pub)
				.account (key.pub)
				.sign (key.prv, key.pub)
				.work (*system.work.generate (key.pub))
				.build_shared ();
	ASSERT_EQ (0, node1.gap_cache.size ());
	node1.block_processor.add (send2);
	node1.block_processor.flush ();
	ASSERT_EQ (1, node1.gap_cache.size ());
	node1.block_processor.add (open);
	node1.block_processor.flush ();
	ASSERT_EQ (2, node1.gap_cache.size ());
	node1.block_processor.add (send1);
	node1.block_processor.flush ();
	ASSERT_TIMELY_EQ (5s, node1.gap_cache.size (), 0);
	ASSERT_TIMELY (5s, node1.store.block.exists (node1.store.tx_begin_read (), send1->hash ()));
	ASSERT_TIMELY (5s, node1.store.block.exists (node1.store.tx_begin_read (), send2->hash ()));
	ASSERT_TIMELY (5s, node1.store.block.exists (node1.store.tx_begin_read (), open->hash ()));
}
