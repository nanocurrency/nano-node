#include <nano/node/election_scheduler.hpp>
#include <nano/node/testing.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <chrono>

using namespace std::chrono_literals;

TEST (election_scheduler, construction)
{
	nano::system system{ 1 };
}

TEST (election_scheduler, activate_one_timely)
{
	nano::system system{ 1 };
	nano::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (nano::dev_genesis_key.pub)
				 .previous (nano::genesis_hash)
				 .representative (nano::dev_genesis_key.pub)
				 .balance (nano::genesis_amount - nano::MBAN_ratio)
				 .link (nano::dev_genesis_key.pub)
				 .sign (nano::dev_genesis_key.prv, nano::dev_genesis_key.pub)
				 .work (*system.work.generate (nano::genesis_hash))
				 .build_shared ();
	system.nodes[0]->ledger.process (system.nodes[0]->store.tx_begin_write (), *send1);
	system.nodes[0]->scheduler.activate (nano::dev_genesis_key.pub, system.nodes[0]->store.tx_begin_read ());
	ASSERT_TIMELY (1s, system.nodes[0]->active.election (send1->qualified_root ()));
}

TEST (election_scheduler, activate_one_flush)
{
	nano::system system{ 1 };
	nano::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (nano::dev_genesis_key.pub)
				 .previous (nano::genesis_hash)
				 .representative (nano::dev_genesis_key.pub)
				 .balance (nano::genesis_amount - nano::MBAN_ratio)
				 .link (nano::dev_genesis_key.pub)
				 .sign (nano::dev_genesis_key.prv, nano::dev_genesis_key.pub)
				 .work (*system.work.generate (nano::genesis_hash))
				 .build_shared ();
	system.nodes[0]->ledger.process (system.nodes[0]->store.tx_begin_write (), *send1);
	system.nodes[0]->scheduler.activate (nano::dev_genesis_key.pub, system.nodes[0]->store.tx_begin_read ());
	system.nodes[0]->scheduler.flush ();
	ASSERT_NE (nullptr, system.nodes[0]->active.election (send1->qualified_root ()));
}

TEST (election_scheduler, no_vacancy)
{
	nano::system system;
	nano::node_config config{ nano::get_available_port (), system.logging };
	config.active_elections_size = 1;
	auto & node = *system.add_node (config);
	nano::state_block_builder builder;
	nano::keypair key;

	// Activating accounts depends on confirmed dependencies. First, prepare 2 accounts
	auto send = builder.make_block ()
				.account (nano::dev_genesis_key.pub)
				.previous (nano::genesis_hash)
				.representative (nano::dev_genesis_key.pub)
				.link (key.pub)
				.balance (nano::genesis_amount - nano::MBAN_ratio)
				.sign (nano::dev_genesis_key.prv, nano::dev_genesis_key.pub)
				.work (*system.work.generate (nano::genesis_hash))
				.build_shared ();
	auto receive = builder.make_block ()
				   .account (key.pub)
				   .previous (0)
				   .representative (key.pub)
				   .link (send->hash ())
				   .balance (nano::MBAN_ratio)
				   .sign (key.prv, key.pub)
				   .work (*system.work.generate (key.pub))
				   .build_shared ();
	ASSERT_EQ (nano::process_result::progress, node.process (*send).code);
	nano::blocks_confirm (node, { send }, true);
	ASSERT_TIMELY (1s, node.active.empty ());
	ASSERT_EQ (nano::process_result::progress, node.process (*receive).code);
	nano::blocks_confirm (node, { receive }, true);
	ASSERT_TIMELY (1s, node.active.empty ());

	// Second, process two eligble transactions
	auto block0 = builder.make_block ()
				  .account (nano::dev_genesis_key.pub)
				  .previous (send->hash ())
				  .representative (nano::dev_genesis_key.pub)
				  .link (nano::dev_genesis_key.pub)
				  .balance (nano::genesis_amount - 2 * nano::MBAN_ratio)
				  .sign (nano::dev_genesis_key.prv, nano::dev_genesis_key.pub)
				  .work (*system.work.generate (send->hash ()))
				  .build_shared ();
	auto block1 = builder.make_block ()
				  .account (key.pub)
				  .previous (receive->hash ())
				  .representative (key.pub)
				  .link (key.pub)
				  .balance (0)
				  .sign (key.prv, key.pub)
				  .work (*system.work.generate (receive->hash ()))
				  .build_shared ();
	ASSERT_EQ (nano::process_result::progress, node.process (*block0).code);
	ASSERT_EQ (nano::process_result::progress, node.process (*block1).code);
	node.scheduler.activate (nano::dev_genesis_key.pub, node.store.tx_begin_read ());
	// There is vacancy so it should be inserted
	ASSERT_TIMELY (1s, node.active.size () == 1);
	node.scheduler.activate (key.pub, node.store.tx_begin_read ());
	// There is no vacancy so it should stay queued
	ASSERT_TIMELY (1s, node.scheduler.size () == 1);
	auto election3 = node.active.election (block0->qualified_root ());
	ASSERT_NE (nullptr, election3);
	election3->force_confirm ();
	// Election completed, next in queue should begin
	ASSERT_TIMELY (1s, node.scheduler.size () == 0);
	ASSERT_TIMELY (1s, node.active.size () == 1);
	auto election4 = node.active.election (block1->qualified_root ());
	ASSERT_NE (nullptr, election4);
}

// Ensure that election_scheduler::flush terminates even if no elections can currently be queued e.g. shutdown or no active_transactions vacancy
TEST (election_scheduler, flush_vacancy)
{
	nano::system system;
	nano::node_config config{ nano::get_available_port (), system.logging };
	// No elections can be activated
	config.active_elections_size = 0;
	auto & node = *system.add_node (config);
	nano::state_block_builder builder;
	nano::keypair key;

	auto send = builder.make_block ()
				.account (nano::dev_genesis_key.pub)
				.previous (nano::genesis_hash)
				.representative (nano::dev_genesis_key.pub)
				.link (key.pub)
				.balance (nano::genesis_amount - nano::MBAN_ratio)
				.sign (nano::dev_genesis_key.prv, nano::dev_genesis_key.pub)
				.work (*system.work.generate (nano::genesis_hash))
				.build_shared ();
	ASSERT_EQ (nano::process_result::progress, node.process (*send).code);
	node.scheduler.activate (nano::dev_genesis_key.pub, node.store.tx_begin_read ());
	// Ensure this call does not block, even though no elections can be activated.
	node.scheduler.flush ();
	ASSERT_EQ (0, node.active.size ());
	ASSERT_EQ (1, node.scheduler.size ());
}
