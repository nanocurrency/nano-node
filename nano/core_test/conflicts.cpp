#include <nano/node/election.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/variant/get.hpp>

using namespace std::chrono_literals;

TEST (conflicts, start_stop)
{
	nano::system system (1);
	auto & node1 (*system.nodes[0]);
	nano::keypair key1;
	auto send1 (std::make_shared<nano::send_block> (nano::dev::genesis->hash (), key1.pub, 0, nano::dev::genesis_key.prv, nano::dev::genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	ASSERT_EQ (nano::process_result::progress, node1.process (*send1).code);
	ASSERT_EQ (0, node1.active.size ());
	node1.scheduler.activate (nano::dev::genesis_key.pub, node1.store.tx_begin_read ());
	node1.scheduler.flush ();
	auto election1 = node1.active.election (send1->qualified_root ());
	ASSERT_EQ (1, node1.active.size ());
	ASSERT_NE (nullptr, election1);
	ASSERT_EQ (1, election1->votes ().size ());
}

TEST (conflicts, add_existing)
{
	nano::system system (1);
	auto & node1 (*system.nodes[0]);
	nano::keypair key1;
	auto send1 (std::make_shared<nano::send_block> (nano::dev::genesis->hash (), key1.pub, 0, nano::dev::genesis_key.prv, nano::dev::genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	ASSERT_EQ (nano::process_result::progress, node1.process (*send1).code);
	node1.scheduler.activate (nano::dev::genesis_key.pub, node1.store.tx_begin_read ());
	nano::keypair key2;
	auto send2 (std::make_shared<nano::send_block> (nano::dev::genesis->hash (), key2.pub, 0, nano::dev::genesis_key.prv, nano::dev::genesis_key.pub, 0));
	send2->sideband_set ({});
	node1.scheduler.activate (nano::dev::genesis_key.pub, node1.store.tx_begin_read ());
	node1.scheduler.flush ();
	auto election1 = node1.active.election (send2->qualified_root ());
	ASSERT_NE (nullptr, election1);
	ASSERT_EQ (1, node1.active.size ());
	auto vote1 (std::make_shared<nano::vote> (key2.pub, key2.prv, 0, send2));
	node1.active.vote (vote1);
	ASSERT_EQ (2, election1->votes ().size ());
	auto votes (election1->votes ());
	ASSERT_NE (votes.end (), votes.find (key2.pub));
}

// Test disabled because it's failing intermittently.
// PR in which it got disabled: https://github.com/nanocurrency/nano-node/pull/3536
// Issue for investigating it: https://github.com/nanocurrency/nano-node/issues/3535
TEST (conflicts, DISABLED_add_two)
{
	nano::system system (1);
	auto & node1 (*system.nodes[0]);
	nano::keypair key1;
	auto send1 (std::make_shared<nano::send_block> (nano::dev::genesis->hash (), key1.pub, 0, nano::dev::genesis_key.prv, nano::dev::genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	ASSERT_EQ (nano::process_result::progress, node1.process (*send1).code);
	node1.block_confirm (send1);
	node1.active.election (send1->qualified_root ())->force_confirm ();
	nano::keypair key2;
	auto send2 (std::make_shared<nano::send_block> (send1->hash (), key2.pub, 0, nano::dev::genesis_key.prv, nano::dev::genesis_key.pub, 0));
	node1.work_generate_blocking (*send2);
	ASSERT_EQ (nano::process_result::progress, node1.process (*send2).code);
	node1.scheduler.activate (nano::dev::genesis_key.pub, node1.store.tx_begin_read ());
	node1.scheduler.flush ();
	ASSERT_EQ (2, node1.active.size ());
}

TEST (conflicts, add_two)
{
	nano::system system{};
	auto const & node = system.add_node ();

	// define a functor that sends from given account to given destination,
	// optionally force-confirming the send blocks *and* receiving on the destination account;
	// the functor returns a pair of the send and receive blocks created or nullptrs if something failed
	//
	auto const do_send = [&node] (auto const & previous, auto const & from, auto const & to, bool forceConfirm = true)
	-> std::pair<std::shared_ptr<nano::block>, std::shared_ptr<nano::block>> {
		auto send = std::make_shared<nano::send_block> (previous->hash (), to.pub, 0, from.prv, from.pub, 0);
		node->work_generate_blocking (*send);

		if (nano::process_result::progress != node->process (*send).code)
		{
			send.reset ();
			return std::make_pair (std::move (send), std::move (send));
		}

		if (forceConfirm)
		{
			node->block_confirm (send);
			node->active.election (send->qualified_root ())->force_confirm ();

			auto receive = std::make_shared<nano::open_block> (send->hash (), to.pub, to.pub, to.prv, to.pub, 0);
			node->work_generate_blocking (*receive);

			if (nano::process_result::progress != node->process (*receive).code)
			{
				return std::make_pair (nullptr, nullptr);
			}

			node->block_confirm (receive);
			node->active.election (receive->qualified_root ())->force_confirm ();

			return std::make_pair (std::move (send), std::move (receive));
		}

		return std::make_pair (std::move (send), nullptr);
	};

	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);

	// send from genesis to account1 and receive it on account1
	//
	nano::keypair account1{};
	auto const [send1, receive1] = do_send (nano::dev::genesis, nano::dev::genesis_key, account1);
	ASSERT_TRUE (send1 && receive1);
	// both blocks having been fully confirmed, we expect 1 (genesis) + 2 (send/receive) = 3 cemented blocks
	//
	ASSERT_TIMELY (3s, 3 == node->ledger.cache.cemented_count);

	// send from genesis to account2 and receive it on account2
	//
	nano::keypair account2{};
	auto const [send2, receive2] = do_send (send1, nano::dev::genesis_key, account2);
	ASSERT_TRUE (send2 && receive2);
	ASSERT_TIMELY (3s, 5 == node->ledger.cache.cemented_count);

	// send from account1 to account3 but do not receive it on account3 and do not force confirm the send block
	//
	nano::keypair account3{};
	{
		auto const [send3, _] = do_send (receive1, account1, account3, false);
		ASSERT_TRUE (send3);
		// expect the number of cemented blocks not to have changed since the last operation
		//
		ASSERT_TIMELY (3s, 5 == node->ledger.cache.cemented_count);
	}

	// send from account1 to account3 but do not receive it on account3 and do not force confirm the send block
	//
	{
		auto const [send4, _] = do_send (receive2, account2, account3, false);
		ASSERT_TRUE (send4);
		// expect the number of cemented blocks not to have changed since the last operation
		//
		ASSERT_TIMELY (3s, 5 == node->ledger.cache.cemented_count);
	}

	// activate elections for the previous two send blocks (to account3) that we did not forcefully confirm
	//
	node->scheduler.activate (account3.pub, node->store.tx_begin_read ());
	node->scheduler.flush ();

	// wait 3s before asserting just to make sure there would be enough time
	// for the Active Elections Container to evict both elections in case they would wrongfully get confirmed
	//
	std::this_thread::sleep_for (3s);
	ASSERT_EQ (2, node->active.size ());
}

TEST (vote_uniquer, null)
{
	nano::block_uniquer block_uniquer;
	nano::vote_uniquer uniquer (block_uniquer);
	ASSERT_EQ (nullptr, uniquer.unique (nullptr));
}

// Show that an identical vote can be uniqued
TEST (vote_uniquer, same_vote)
{
	nano::block_uniquer block_uniquer;
	nano::vote_uniquer uniquer (block_uniquer);
	nano::keypair key;
	auto vote1 (std::make_shared<nano::vote> (key.pub, key.prv, 0, std::make_shared<nano::state_block> (0, 0, 0, 0, 0, key.prv, key.pub, 0)));
	auto vote2 (std::make_shared<nano::vote> (*vote1));
	ASSERT_EQ (vote1, uniquer.unique (vote1));
	ASSERT_EQ (vote1, uniquer.unique (vote2));
}

// Show that a different vote for the same block will have the block uniqued
TEST (vote_uniquer, same_block)
{
	nano::block_uniquer block_uniquer;
	nano::vote_uniquer uniquer (block_uniquer);
	nano::keypair key1;
	nano::keypair key2;
	auto block1 (std::make_shared<nano::state_block> (0, 0, 0, 0, 0, key1.prv, key1.pub, 0));
	auto block2 (std::make_shared<nano::state_block> (*block1));
	auto vote1 (std::make_shared<nano::vote> (key1.pub, key1.prv, 0, block1));
	auto vote2 (std::make_shared<nano::vote> (key1.pub, key1.prv, 0, block2));
	ASSERT_EQ (vote1, uniquer.unique (vote1));
	ASSERT_EQ (vote2, uniquer.unique (vote2));
	ASSERT_NE (vote1, vote2);
	ASSERT_EQ (boost::get<std::shared_ptr<nano::block>> (vote1->blocks[0]), boost::get<std::shared_ptr<nano::block>> (vote2->blocks[0]));
}

TEST (vote_uniquer, vbh_one)
{
	nano::block_uniquer block_uniquer;
	nano::vote_uniquer uniquer (block_uniquer);
	nano::keypair key;
	auto block (std::make_shared<nano::state_block> (0, 0, 0, 0, 0, key.prv, key.pub, 0));
	std::vector<nano::block_hash> hashes;
	hashes.push_back (block->hash ());
	auto vote1 (std::make_shared<nano::vote> (key.pub, key.prv, 0, hashes));
	auto vote2 (std::make_shared<nano::vote> (*vote1));
	ASSERT_EQ (vote1, uniquer.unique (vote1));
	ASSERT_EQ (vote1, uniquer.unique (vote2));
}

TEST (vote_uniquer, vbh_two)
{
	nano::block_uniquer block_uniquer;
	nano::vote_uniquer uniquer (block_uniquer);
	nano::keypair key;
	auto block1 (std::make_shared<nano::state_block> (0, 0, 0, 0, 0, key.prv, key.pub, 0));
	std::vector<nano::block_hash> hashes1;
	hashes1.push_back (block1->hash ());
	auto block2 (std::make_shared<nano::state_block> (1, 0, 0, 0, 0, key.prv, key.pub, 0));
	std::vector<nano::block_hash> hashes2;
	hashes2.push_back (block2->hash ());
	auto vote1 (std::make_shared<nano::vote> (key.pub, key.prv, 0, hashes1));
	auto vote2 (std::make_shared<nano::vote> (key.pub, key.prv, 0, hashes2));
	ASSERT_EQ (vote1, uniquer.unique (vote1));
	ASSERT_EQ (vote2, uniquer.unique (vote2));
}

TEST (vote_uniquer, cleanup)
{
	nano::block_uniquer block_uniquer;
	nano::vote_uniquer uniquer (block_uniquer);
	nano::keypair key;
	auto vote1 (std::make_shared<nano::vote> (key.pub, key.prv, 0, std::make_shared<nano::state_block> (0, 0, 0, 0, 0, key.prv, key.pub, 0)));
	auto vote2 (std::make_shared<nano::vote> (key.pub, key.prv, nano::vote::timestamp_min * 1, std::make_shared<nano::state_block> (0, 0, 0, 0, 0, key.prv, key.pub, 0)));
	auto vote3 (uniquer.unique (vote1));
	auto vote4 (uniquer.unique (vote2));
	vote2.reset ();
	vote4.reset ();
	ASSERT_EQ (2, uniquer.size ());
	auto iterations (0);
	while (uniquer.size () == 2)
	{
		auto vote5 (uniquer.unique (vote1));
		ASSERT_LT (iterations++, 200);
	}
}
