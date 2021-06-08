#include <nano/node/ledger_walker.hpp>
#include <nano/node/testing.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <numeric>

using namespace std::chrono_literals;

TEST (leger_walker, first_test)
{
	nano::system system{};

	nano::node_config node_config (nano::get_available_port (), system.logging);
	node_config.enable_voting = true;

	const auto node = system.add_node (node_config);
	std::array<nano::keypair, 3> keys{};

	system.wallet (0)->insert_adhoc (nano::dev_genesis_key.prv);
	for (auto itr = 0; itr != keys.size (); ++itr)
	{
		system.wallet (0)->insert_adhoc (keys[itr].prv);
		const auto block = system.wallet (0)->send_action (nano::dev_genesis_key.pub, keys[itr].pub, nano::Gxrb_ratio);
		ASSERT_TIMELY (3s, 1 + (itr + 1) * 2 == node->ledger.cache.cemented_count);
	}

	std::vector<nano::uint128_t> amounts_to_send (10);
	std::iota (amounts_to_send.begin (), amounts_to_send.end (), nano::Mxrb_ratio);

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

	/*
    This is how we expect chains to look like (for 3 accounts and 10 amounts to be sent).
    k1: Gx     SEND     02     SEND     05     SEND     08     SEND
    k2: Gx     00       SEND   03     SEND     06     SEND     09
    k3: Gx     01       SEND   04     SEND     07     SEND
    */

	std::vector<nano::uint128_t> amounts_expected{ nano::Mxrb_ratio + 9, nano::Mxrb_ratio + 8, nano::Mxrb_ratio + 7, nano::Mxrb_ratio + 4, nano::Mxrb_ratio + 3,
		nano::Mxrb_ratio + 2, nano::Gxrb_ratio, nano::Mxrb_ratio, nano::Gxrb_ratio, nano::Mxrb_ratio + 1, nano::Gxrb_ratio,
		nano::Mxrb_ratio + 5, nano::Mxrb_ratio + 6 };
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
