#include <nano/lib/blocks.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/election.hpp>
#include <nano/node/scheduler/component.hpp>
#include <nano/node/scheduler/priority.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <chrono>

using namespace std::chrono_literals;

TEST (election_scheduler, construction)
{
	nano::test::system system{ 1 };
}

TEST (election_scheduler, activate_one_timely)
{
	nano::test::system system{ 1 };
	nano::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio)
				 .link (nano::dev::genesis_key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();
	system.nodes[0]->ledger.process (system.nodes[0]->ledger.tx_begin_write (), send1);
	system.nodes[0]->scheduler.priority.activate (system.nodes[0]->ledger.tx_begin_read (), nano::dev::genesis_key.pub);
	ASSERT_TIMELY (5s, system.nodes[0]->active.election (send1->qualified_root ()));
}

TEST (election_scheduler, activate_one_flush)
{
	nano::test::system system{ 1 };
	nano::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio)
				 .link (nano::dev::genesis_key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();
	system.nodes[0]->ledger.process (system.nodes[0]->ledger.tx_begin_write (), send1);
	system.nodes[0]->scheduler.priority.activate (system.nodes[0]->ledger.tx_begin_read (), nano::dev::genesis_key.pub);
	ASSERT_TIMELY (5s, system.nodes[0]->active.election (send1->qualified_root ()));
}
