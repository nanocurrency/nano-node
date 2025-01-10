#include <celerix/lib/blocks.hpp>
#include <celerix/node/active_elections.hpp>
#include <celerix/secure/ledger.hpp>
#include <celerix/test_common/chains.hpp>
#include <celerix/test_common/system.hpp>
#include <celerix/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <numeric>

using namespace std::chrono_literals;

/*
 * Ensures all not confirmed accounts get activated by backlog scan periodically
 */
TEST (backlog, population)
{
	celerix::mutex mutex;
	std::unordered_set<celerix::account> activated;

	celerix::test::system system{};
	auto & node = *system.add_node ();

	node.backlog_scan.batch_activated.add ([&] (auto const & batch) {
		celerix::lock_guard<celerix::mutex> lock{ mutex };
		for (auto const & info : batch)
		{
			activated.insert (info.account);
		}
	});

	auto blocks = celerix::test::setup_independent_blocks (system, node, 256);

	// Checks if `activated` set contains all accounts we previously set up
	auto all_activated = [&] () {
		celerix::lock_guard<celerix::mutex> lock{ mutex };
		return std::all_of (blocks.begin (), blocks.end (), [&] (auto const & item) {
			return activated.count (item->account ()) != 0;
		});
	};
	ASSERT_TIMELY (5s, all_activated ());

	// Clear activated set to ensure we activate those accounts more than once
	{
		celerix::lock_guard<celerix::mutex> lock{ mutex };
		activated.clear ();
	}

	ASSERT_TIMELY (5s, all_activated ());
}

/*
 * Ensures that elections are activated without live traffic
 */
TEST (backlog, election_activation)
{
	celerix::test::system system;
	celerix::node_config node_config = system.default_config ();
	auto & node = *system.add_node (node_config);
	celerix::keypair key;
	celerix::block_builder builder;
	auto send = builder
				.state ()
				.account (celerix::dev::genesis_key.pub)
				.previous (celerix::dev::genesis->hash ())
				.representative (celerix::dev::genesis_key.pub)
				.balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				.link (key.pub)
				.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				.work (*node.work_generate_blocking (celerix::dev::genesis->hash ()))
				.build ();
	{
		auto transaction = node.ledger.tx_begin_write ();
		ASSERT_EQ (celerix::block_status::progress, node.ledger.process (transaction, send));
	}
	ASSERT_TIMELY_EQ (5s, node.active.size (), 1);
}