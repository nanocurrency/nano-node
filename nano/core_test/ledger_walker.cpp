#include <nano/node/ledger_walker.hpp>
#include <nano/node/testing.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <numeric>

using namespace std::chrono_literals;

TEST (ledger_walker, staircase_geometry)
{
	nano::system system{};

	nano::node_config node_config (nano::get_available_port (), system.logging);
	node_config.enable_voting = true;
    node_config.receive_minimum = 1;

	const auto node = system.add_node (node_config);
	std::array<nano::keypair, 3> keys{};

	system.wallet (0)->insert_adhoc (nano::dev_genesis_key.prv);
	for (auto itr = 0; itr != keys.size (); ++itr)
	{
		system.wallet (0)->insert_adhoc (keys[itr].prv);
		const auto block = system.wallet (0)->send_action (nano::dev_genesis_key.pub, keys[itr].pub, 1000);
		ASSERT_TIMELY (3s, 1 + (itr + 1) * 2 == node->ledger.cache.cemented_count);
	}

	std::vector<nano::uint128_t> amounts_to_send (10);
	std::iota (amounts_to_send.begin (), amounts_to_send.end (), 1);

	const nano::account * last_destination{};
	for (auto itr = 0; itr != amounts_to_send.size (); ++itr)
	{
		const auto source_index = itr % keys.size ();
		const auto destination_index = (source_index + 1) % keys.size ();
		last_destination = &keys[destination_index].pub;

		const auto send = system.wallet (0)->send_action (keys[source_index].pub, keys[destination_index].pub, amounts_to_send[itr]);
		ASSERT_TRUE (send);

		ASSERT_TIMELY (3s, 1 + keys.size () * 2 + (itr + 1) * 2 == node->ledger.cache.cemented_count);
	}

	ASSERT_TRUE (last_destination);
	const auto transaction = node->ledger.store.tx_begin_read ();
	nano::account_info last_destination_info{};
	const auto last_destination_read_error = node->ledger.store.account.get (transaction, *last_destination, last_destination_info);
	ASSERT_FALSE (last_destination_read_error);

    // This is how we expect chains to look like (for 3 accounts and 10 amounts to be sent)
    // k1: 1000     SEND     3     SEND     6     SEND     9     SEND
    // k2: 1000     1       SEND   4     SEND     7     SEND     10
    // k3: 1000     2       SEND   5     SEND     8     SEND

	std::vector<nano::uint128_t> amounts_expected{ 10, 9, 8, 5, 4, 3, 1000, 1, 1000, 2, 1000, 6, 7 };
	auto amounts_expected_itr = amounts_expected.cbegin ();

	nano::ledger_walker ledger_walker{ node->ledger };
	ledger_walker.walk_backward (last_destination_info.head, [&] (const auto & block) {
		if (block->sideband ().details.is_receive)
		{
			nano::amount previous_balance{};
			if (!block->previous ().is_zero ())
			{
				const auto previous_block = node->ledger.store.block_get_no_sideband (transaction, block->previous ());
				previous_balance = previous_block->balance ();
			}

			EXPECT_EQ (*amounts_expected_itr++, block->balance ().number () - previous_balance.number ());
		}

		return true;
	});
}
