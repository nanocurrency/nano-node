#include <nano/node/bootstrap/bootstrap_ascending.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

namespace
{
nano::block_hash random_hash ()
{
	nano::block_hash random_hash;
	nano::random_pool::generate_block (random_hash.bytes.data (), random_hash.bytes.size ());
	return random_hash;
}
}

TEST (account_sets, construction)
{
	nano::bootstrap::bootstrap_ascending::account_sets sets;
}

TEST (account_sets, empty_blocked)
{
	nano::account account{ 1 };
	nano::bootstrap::bootstrap_ascending::account_sets sets;
	ASSERT_FALSE (sets.blocked (account));
}

TEST (account_sets, block)
{
	nano::account account{ 1 };
	nano::bootstrap::bootstrap_ascending::account_sets sets;
	sets.block (account, random_hash ());
	ASSERT_TRUE (sets.blocked (account));
}

TEST (account_sets, unblock)
{
	nano::account account{ 1 };
	nano::bootstrap::bootstrap_ascending::account_sets sets;
	auto hash = random_hash ();
	sets.block (account, hash);
	sets.unblock (account, hash);
	ASSERT_FALSE (sets.blocked (account));
}

/**
 * Tests basic construction of a bootstrap_ascending attempt
 */
TEST (bootstrap_ascending, construction)
{
	nano::test::system system{ 1 };
	auto & node = *system.nodes[0];
	auto ascending = std::make_shared<nano::bootstrap::bootstrap_ascending> (node);
}

/**
 * Tests that bootstrap_ascending attempt can run and complete
 */
TEST (bootstrap_ascending, start_stop)
{
	nano::test::system system{ 2 };
	auto & node = *system.nodes[0];
	ASSERT_TIMELY (5s, node.stats.count (nano::stat::type::bootstrap, nano::stat::detail::initiate_ascending, nano::stat::dir::out) > 0);
}

/**
 * Tests the base case for returning
 */
TEST (bootstrap_ascending, account_base)
{
	nano::node_flags flags;
	nano::test::system system{ 1, nano::transport::transport_type::tcp, flags };
	auto & node0 = *system.nodes[0];
	nano::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .link (0)
				 .balance (nano::dev::constants.genesis_amount - 1)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build_shared ();
	ASSERT_EQ (nano::process_result::progress, node0.process (*send1).code);
	auto & node1 = *system.add_node (flags);
	ASSERT_TIMELY (5s, node1.block (send1->hash ()) != nullptr);
}

/**
 * Tests that bootstrap_ascending will return multiple new blocks in-order
 */
TEST (bootstrap_ascending, account_inductive)
{
	nano::node_flags flags;
	nano::test::system system{ 1, nano::transport::transport_type::tcp, flags };
	auto & node0 = *system.nodes[0];
	nano::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .link (0)
				 .balance (nano::dev::constants.genesis_amount - 1)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build_shared ();
	auto send2 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .link (0)
				 .balance (nano::dev::constants.genesis_amount - 2)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build_shared ();
	std::cerr << "Genesis: " << nano::dev::genesis->hash ().to_string () << std::endl;
	std::cerr << "Send1: " << send1->hash ().to_string () << std::endl;
	std::cerr << "Send2: " << send2->hash ().to_string () << std::endl;
	ASSERT_EQ (nano::process_result::progress, node0.process (*send1).code);
	ASSERT_EQ (nano::process_result::progress, node0.process (*send2).code);
	auto & node1 = *system.add_node (flags);
	ASSERT_TIMELY (50s, node1.block (send2->hash ()) != nullptr);
}

/**
 * Tests that bootstrap_ascending will return multiple new blocks in-order
 */
TEST (bootstrap_ascending, trace_base)
{
	nano::node_flags flags;
	flags.disable_legacy_bootstrap = true;
	nano::test::system system{ 1, nano::transport::transport_type::tcp, flags };
	auto & node0 = *system.nodes[0];
	nano::keypair key;
	nano::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .link (key.pub)
				 .balance (nano::dev::constants.genesis_amount - 1)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build_shared ();
	auto receive1 = builder.make_block ()
					.account (key.pub)
					.previous (0)
					.representative (nano::dev::genesis_key.pub)
					.link (send1->hash ())
					.balance (1)
					.sign (key.prv, key.pub)
					.work (*system.work.generate (key.pub))
					.build_shared ();
	std::cerr << "Genesis key: " << nano::dev::genesis_key.pub.to_account () << std::endl;
	std::cerr << "Key: " << key.pub.to_account () << std::endl;
	std::cerr << "Genesis: " << nano::dev::genesis->hash ().to_string () << std::endl;
	std::cerr << "send1: " << send1->hash ().to_string () << std::endl;
	std::cerr << "receive1: " << receive1->hash ().to_string () << std::endl;
	auto & node1 = *system.add_node ();
	std::cerr << "--------------- Start ---------------\n";
	ASSERT_EQ (nano::process_result::progress, node0.process (*send1).code);
	ASSERT_EQ (nano::process_result::progress, node0.process (*receive1).code);
	ASSERT_EQ (node1.store.pending.begin (node1.store.tx_begin_read (), nano::pending_key{ key.pub, 0 }), node1.store.pending.end ());
	std::cerr << "node0: " << node0.network.endpoint () << std::endl;
	std::cerr << "node1: " << node1.network.endpoint () << std::endl;
	ASSERT_TIMELY (10s, node1.block (receive1->hash ()) != nullptr);
}
