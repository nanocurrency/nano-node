#include <celerix/lib/blocks.hpp>
#include <celerix/node/active_elections.hpp>
#include <celerix/node/election.hpp>
#include <celerix/node/scheduler/component.hpp>
#include <celerix/node/scheduler/priority.hpp>
#include <celerix/secure/ledger.hpp>
#include <celerix/secure/vote.hpp>
#include <celerix/test_common/chains.hpp>
#include <celerix/test_common/system.hpp>
#include <celerix/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/variant/get.hpp>

using namespace std::chrono_literals;

TEST (conflicts, start_stop)
{
	celerix::test::system system (1);
	auto & node1 (*system.nodes[0]);
	celerix::keypair key1;
	celerix::block_builder builder;
	auto send1 = builder
				 .send ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (0)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (0)
				 .build ();
	node1.work_generate_blocking (*send1);
	ASSERT_EQ (celerix::block_status::progress, node1.process (send1));
	ASSERT_EQ (0, node1.active.size ());
	auto election1 = celerix::test::start_election (system, node1, send1->hash ());
	ASSERT_EQ (1, node1.active.size ());
	ASSERT_NE (nullptr, election1);
	ASSERT_EQ (1, election1->votes ().size ());
}

TEST (conflicts, add_existing)
{
	celerix::test::system system{ 1 };
	auto & node1 = *system.nodes[0];
	celerix::keypair key1;

	// create a send block to send all of the celerix supply to key1
	celerix::block_builder builder;
	auto send1 = builder
				 .send ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (0)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (0)
				 .build ();
	node1.work_generate_blocking (*send1);

	// add the block to ledger as an unconfirmed block
	ASSERT_EQ (celerix::block_status::progress, node1.process (send1));

	// wait for send1 to be inserted in the ledger
	ASSERT_TIMELY (5s, node1.block (send1->hash ()));

	// instruct the election scheduler to trigger an election for send1
	celerix::test::start_election (system, node1, send1->hash ());

	// wait for election to be started before processing send2
	ASSERT_TIMELY (5s, node1.active.active (*send1));

	celerix::keypair key2;
	auto send2 = builder
				 .send ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (0)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (0)
				 .build ();
	node1.work_generate_blocking (*send2);
	send2->sideband_set ({});

	// the block processor will notice that the block is a fork and it will try to publish it
	// which will update the election object
	node1.block_processor.add (send2);

	ASSERT_TRUE (node1.active.active (*send1));
	ASSERT_TIMELY (5s, node1.active.active (*send2));
}

TEST (conflicts, add_two)
{
	celerix::test::system system{};
	auto const & node = system.add_node ();
	celerix::keypair key1, key2, key3;
	auto gk = celerix::dev::genesis_key;

	// create 2 new accounts, that receive 1 raw each, all blocks are force confirmed
	auto [send1, open1] = celerix::test::setup_new_account (system, *node, 1, gk, key1, gk.pub, true);
	auto [send2, open2] = celerix::test::setup_new_account (system, *node, 1, gk, key2, gk.pub, true);
	ASSERT_EQ (5, node->ledger.cemented_count ());

	// send 1 raw to account key3 from key1
	auto send_a = celerix::state_block_builder ()
				  .account (key1.pub)
				  .previous (open1->hash ())
				  .representative (celerix::dev::genesis_key.pub)
				  .balance (0)
				  .link (key3.pub)
				  .sign (key1.prv, key1.pub)
				  .work (*system.work.generate (open1->hash ()))
				  .build ();

	// send 1 raw to account key3 from key2
	auto send_b = celerix::state_block_builder ()
				  .account (key2.pub)
				  .previous (open2->hash ())
				  .representative (celerix::dev::genesis_key.pub)
				  .balance (0)
				  .link (key3.pub)
				  .sign (key2.prv, key2.pub)
				  .work (*system.work.generate (open2->hash ()))
				  .build ();

	// activate elections for the previous two send blocks (to account3) that we did not forcefully confirm
	ASSERT_TRUE (celerix::test::process (*node, { send_a, send_b }));
	ASSERT_TRUE (celerix::test::start_elections (system, *node, { send_a, send_b }));
	ASSERT_TRUE (node->active.election (send_a->qualified_root ()));
	ASSERT_TRUE (node->active.election (send_b->qualified_root ()));
	ASSERT_TIMELY_EQ (5s, node->active.size (), 2);
}

TEST (vote_uniquer, null)
{
	celerix::vote_uniquer uniquer;
	ASSERT_EQ (nullptr, uniquer.unique (nullptr));
}

TEST (vote_uniquer, vbh_one)
{
	celerix::vote_uniquer uniquer;
	celerix::keypair key;
	celerix::block_builder builder;
	auto block = builder
				 .state ()
				 .account (0)
				 .previous (0)
				 .representative (0)
				 .balance (0)
				 .link (0)
				 .sign (key.prv, key.pub)
				 .work (0)
				 .build ();
	std::vector<celerix::block_hash> hashes;
	hashes.push_back (block->hash ());
	auto vote1 = celerix::test::make_vote (key, { hashes }, 0, 0);
	auto vote2 (std::make_shared<celerix::vote> (*vote1));
	ASSERT_EQ (vote1, uniquer.unique (vote1));
	ASSERT_EQ (vote1, uniquer.unique (vote2));
}

TEST (vote_uniquer, vbh_two)
{
	celerix::vote_uniquer uniquer;
	celerix::keypair key;
	celerix::block_builder builder;
	auto block1 = builder
				  .state ()
				  .account (0)
				  .previous (0)
				  .representative (0)
				  .balance (0)
				  .link (0)
				  .sign (key.prv, key.pub)
				  .work (0)
				  .build ();
	std::vector<celerix::block_hash> hashes1;
	hashes1.push_back (block1->hash ());
	auto block2 = builder
				  .state ()
				  .account (1)
				  .previous (0)
				  .representative (0)
				  .balance (0)
				  .link (0)
				  .sign (key.prv, key.pub)
				  .work (0)
				  .build ();
	std::vector<celerix::block_hash> hashes2;
	hashes2.push_back (block2->hash ());
	auto vote1 = celerix::test::make_vote (key, { hashes1 }, 0, 0);
	auto vote2 = celerix::test::make_vote (key, { hashes2 }, 0, 0);
	ASSERT_EQ (vote1, uniquer.unique (vote1));
	ASSERT_EQ (vote2, uniquer.unique (vote2));
}

TEST (vote_uniquer, cleanup)
{
	celerix::vote_uniquer uniquer;
	celerix::keypair key;
	auto vote1 = std::make_shared<celerix::vote> (key.pub, key.prv, 0, 0, std::vector<celerix::block_hash>{ celerix::block_hash{ 0 } });
	auto vote2 = std::make_shared<celerix::vote> (key.pub, key.prv, celerix::vote::timestamp_min * 1, 0, std::vector<celerix::block_hash>{ celerix::block_hash{ 0 } });
	auto vote3 = uniquer.unique (vote1);
	auto vote4 = uniquer.unique (vote2);
	vote2.reset ();
	vote4.reset ();
	ASSERT_EQ (2, uniquer.size ());
	std::this_thread::sleep_for (celerix::block_uniquer::cleanup_cutoff);
	auto vote5 = uniquer.unique (vote1);
	ASSERT_EQ (1, uniquer.size ());
}
