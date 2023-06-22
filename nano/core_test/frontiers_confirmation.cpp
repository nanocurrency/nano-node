#include <nano/node/active_transactions.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (frontiers_confirmation, mode)
{
	nano::keypair key;
	nano::block_builder builder;
	nano::node_flags node_flags;
	// Always mode
	{
		nano::test::system system;
		nano::node_config node_config = system.default_config ();
		node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::always;
		auto node = system.add_node (node_config, node_flags);
		auto send = builder
					.state ()
					.account (nano::dev::genesis_key.pub)
					.previous (nano::dev::genesis->hash ())
					.representative (nano::dev::genesis_key.pub)
					.balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio)
					.link (key.pub)
					.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					.work (*node->work_generate_blocking (nano::dev::genesis->hash ()))
					.build ();
		{
			auto transaction = node->store.tx_begin_write ();
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send).code);
		}
		ASSERT_TIMELY (5s, node->active.size () == 1);
	}
	// Auto mode
	{
		nano::test::system system;
		nano::node_config node_config = system.default_config ();
		node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::automatic;
		auto node = system.add_node (node_config, node_flags);
		auto send = builder
					.state ()
					.account (nano::dev::genesis_key.pub)
					.previous (nano::dev::genesis->hash ())
					.representative (nano::dev::genesis_key.pub)
					.balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio)
					.link (key.pub)
					.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					.work (*node->work_generate_blocking (nano::dev::genesis->hash ()))
					.build ();
		{
			auto transaction = node->store.tx_begin_write ();
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send).code);
		}
		ASSERT_TIMELY (5s, node->active.size () == 1);
	}
	// Disabled mode
	{
		nano::test::system system;
		nano::node_config node_config = system.default_config ();
		node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
		auto node = system.add_node (node_config, node_flags);
		auto send = builder
					.state ()
					.account (nano::dev::genesis_key.pub)
					.previous (nano::dev::genesis->hash ())
					.representative (nano::dev::genesis_key.pub)
					.balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio)
					.link (key.pub)
					.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					.work (*node->work_generate_blocking (nano::dev::genesis->hash ()))
					.build ();
		{
			auto transaction = node->store.tx_begin_write ();
			ASSERT_EQ (nano::process_result::progress, node->ledger.process (transaction, *send).code);
		}
		system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
		std::this_thread::sleep_for (std::chrono::seconds (1));
		ASSERT_EQ (0, node->active.size ());
	}
}
