#include <nano/node/active_transactions.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <numeric>

using namespace std::chrono_literals;

namespace
{
using block_list_t = std::vector<std::shared_ptr<nano::block>>;

/*
 * Creates `count` 1 raw sends from genesis to unique accounts and corresponding open blocks.
 * The genesis chain is then confirmed, but leaves open blocks unconfirmed
 * The list of unconfirmed open blocks is returned.
 */
block_list_t setup_independent_blocks (nano::test::system & system, nano::node & node, int count)
{
	std::vector<std::shared_ptr<nano::block>> blocks;

	auto latest = node.latest (nano::dev::genesis_key.pub);
	auto balance = node.balance (nano::dev::genesis_key.pub);

	for (int n = 0; n < count; ++n)
	{
		nano::keypair key;
		nano::block_builder builder;

		balance -= 1;
		auto send = builder
					.state ()
					.account (nano::dev::genesis_key.pub)
					.previous (latest)
					.representative (nano::dev::genesis_key.pub)
					.balance (balance)
					.link (key.pub)
					.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					.work (*system.work.generate (latest))
					.build_shared ();
		latest = send->hash ();

		auto open = builder
					.state ()
					.account (key.pub)
					.previous (0)
					.representative (key.pub)
					.balance (1)
					.link (send->hash ())
					.sign (key.prv, key.pub)
					.work (*system.work.generate (key.pub))
					.build_shared ();

		EXPECT_TRUE (nano::test::process (node, { send, open }));
		EXPECT_TIMELY (5s, nano::test::exists (node, { send, open })); // Ensure blocks are in the ledger

		blocks.push_back (open);
	}

	// Confirm whole genesis chain at once
	EXPECT_TIMELY (5s, nano::test::confirm (node, { latest }));
	EXPECT_TIMELY (5s, nano::test::confirmed (node, { latest }));

	return blocks;
}
}

/*
 * Ensures all not confirmed accounts get activated by backlog scan periodically
 */
TEST (backlog, population)
{
	nano::mutex mutex;
	std::unordered_set<nano::account> activated;
	nano::test::system system{};
	auto & node = *system.add_node ();

	node.backlog.activate_callback.add ([&] (nano::transaction const & transaction, nano::account const & account, nano::account_info const & account_info, nano::confirmation_height_info const & conf_info) {
		nano::lock_guard<nano::mutex> lock{ mutex };

		activated.insert (account);
	});

	auto blocks = setup_independent_blocks (system, node, 256);

	// Checks if `activated` set contains all accounts we previously set up
	auto all_activated = [&] () {
		nano::lock_guard<nano::mutex> lock{ mutex };
		return std::all_of (blocks.begin (), blocks.end (), [&] (auto const & item) {
			auto account = item->account ();
			debug_assert (!account.is_zero ());
			return activated.count (account) != 0;
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
