#include <nano/lib/blocks.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/test_common/chains.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <numeric>

using namespace std::chrono_literals;

/*
 * Ensures all not confirmed accounts get activated by backlog scan periodically
 */
TEST (backlog, population)
{
	nano::mutex mutex;
	std::unordered_set<nano::account> activated;

	nano::test::system system{};
	auto & node = *system.add_node ();

	node.backlog.activate_callback.add ([&] (nano::secure::transaction const & transaction, nano::account const & account) {
		nano::lock_guard<nano::mutex> lock{ mutex };

		activated.insert (account);
	});

	auto blocks = nano::test::setup_independent_blocks (system, node, 256);

	// Checks if `activated` set contains all accounts we previously set up
	auto all_activated = [&] () {
		nano::lock_guard<nano::mutex> lock{ mutex };
		return std::all_of (blocks.begin (), blocks.end (), [&] (auto const & item) {
			return activated.count (item->account ()) != 0;
		});
	};
	ASSERT_TIMELY (5s, all_activated ());

	// Clear activated set to ensure we activate those accounts more than once
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		activated.clear ();
	}

	ASSERT_TIMELY (5s, all_activated ());
}

/*
 * Ensures that elections are activated without live traffic
 */
TEST (backlog, election_activation)
{
	nano::test::system system;
	nano::node_config node_config = system.default_config ();
	auto & node = *system.add_node (node_config);
	nano::keypair key;
	nano::block_builder builder;
	auto send = builder
				.state ()
				.account (nano::dev::genesis_key.pub)
				.previous (nano::dev::genesis->hash ())
				.representative (nano::dev::genesis_key.pub)
				.balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio)
				.link (key.pub)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*node.work_generate_blocking (nano::dev::genesis->hash ()))
				.build ();
	{
		auto transaction = node.ledger.tx_begin_write ();
		ASSERT_EQ (nano::block_status::progress, node.ledger.process (transaction, send));
	}
	ASSERT_TIMELY_EQ (5s, node.active.size (), 1);
}