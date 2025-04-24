#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/node/node.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/test_common/chains.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (block_processor, backlog_throttling)
{
	nano::test::system system;

	nano::node_config node_config;
	// Backlog won't be rolled back, as we want to test the throttling works when backlog is exceeded
	node_config.max_backlog = 5;
	node_config.backlog_scan.enable = false;
	node_config.bounded_backlog.enable = false; // Disable rollbacks
	// Allow at most 4 blocks per second when throttling
	node_config.block_processor.batch_size = 1;
	node_config.block_processor.backlog_throttle = 500ms;
	auto & node = *system.add_node (node_config);

	const int howmany_blocks = 2;
	const int howmany_chains = 5;

	auto chains = nano::test::setup_chains (system, node, howmany_chains, howmany_blocks, nano::dev::genesis_key, /* do not confirm */ false);

	ASSERT_TIMELY_EQ (20s, node.ledger.block_count (), 21);

	// Prepare live spam blocks
	int const spam_count = 20;

	auto latest = node.latest (nano::dev::genesis_key.pub);
	auto balance = node.balance (nano::dev::genesis_key.pub);

	std::vector<std::shared_ptr<nano::block>> blocks;
	for (int n = 0; n < spam_count; ++n)
	{
		nano::keypair throwaway;
		nano::block_builder builder;

		balance -= 1;
		auto send = builder
					.state ()
					.account (nano::dev::genesis_key.pub)
					.previous (latest)
					.representative (nano::dev::genesis_key.pub)
					.balance (balance)
					.link (throwaway.pub)
					.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					.work (*system.work.generate (latest))
					.build ();

		latest = send->hash ();

		blocks.push_back (send);
	}

	// Send them to the node as live blocks
	nano::test::process_live (node, blocks);

	// Ensure no more than 10 blocks are processed in 5 seconds
	ASSERT_NEVER (5s, node.ledger.block_count () > 21 + 10);
}