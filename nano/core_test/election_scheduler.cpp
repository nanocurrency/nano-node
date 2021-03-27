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
	             .balance (nano::genesis_amount - nano::Gxrb_ratio)
	             .link (nano::dev_genesis_key.pub)
	             .sign (nano::dev_genesis_key.prv, nano::dev_genesis_key.pub)
	             .work (*system.work.generate (nano::genesis_hash))
	             .build_shared ();
	system.nodes[0]->ledger.process(system.nodes[0]->store.tx_begin_write (), *send1);
	system.nodes[0]->scheduler.activate (nano::dev_genesis_key.pub);
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
	             .balance (nano::genesis_amount - nano::Gxrb_ratio)
	             .link (nano::dev_genesis_key.pub)
	             .sign (nano::dev_genesis_key.prv, nano::dev_genesis_key.pub)
	             .work (*system.work.generate (nano::genesis_hash))
	             .build_shared ();
	system.nodes[0]->ledger.process(system.nodes[0]->store.tx_begin_write (), *send1);
	system.nodes[0]->scheduler.activate (nano::dev_genesis_key.pub);
	system.nodes[0]->scheduler.flush ();
	ASSERT_NE (nullptr, system.nodes[0]->active.election (send1->qualified_root ()));
}
