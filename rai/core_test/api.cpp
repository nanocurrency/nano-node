#include <gtest/gtest.h>
#include <rai/lib/interface.h>
#include <rai/node/api.hpp>
#include <rai/node/testing.hpp>

TEST (api, address_valid)
{
	rai::inactive_node node;
	nano::api::api_handler handler (*node.node);

	nano::api::req_address_valid request;
	request.set_address ("xrb_invalid");
	auto response = handler.request (request);
	ASSERT_EQ (response.value ()->valid (), false);

	request.set_address ("xrb_1111111111111111111111111111111111111111111111111111hifc8npp");
	response = handler.request (request);
	ASSERT_EQ (response.value ()->valid (), true);

	request.set_address ("nano_1111111111111111111111111111111111111111111111111111hifc8npp");
	response = handler.request (request);
	ASSERT_EQ (response.value ()->valid (), true);
}

TEST (api, ping)
{
	rai::inactive_node node;
	nano::api::api_handler handler (*node.node);

	nano::api::req_ping ping;
	ping.set_id (12345);
	auto pong = handler.request (ping);
	ASSERT_EQ (pong.value ()->id (), 12345);
}

TEST (api, account_pending)
{
	rai::system system (24000, 1);
	rai::keypair key1;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	auto block1 (system.wallet (0)->send_action (rai::test_genesis_key.pub, key1.pub, 100));

	nano::api::api_handler handler (*system.nodes[0]);

	// Query with count and a single account
	nano::api::req_account_pending pending;
	pending.set_count (100);
	pending.add_accounts (key1.pub.to_account ());

	auto res = handler.request (pending);
	ASSERT_EQ (1, res.value ()->pending_size ());
	ASSERT_EQ (1, res.value ()->pending (0).block_info_size ());
	ASSERT_EQ (block1->hash (), res.value ()->pending (0).block_info (0).hash ());

	// Rerun query with source. Make sure we get back the correct amount and a valid source address.
	pending.set_source (true);
	res = handler.request (pending);
	ASSERT_EQ ("100", res.value ()->pending (0).block_info (0).amount ());
	ASSERT_EQ (0, xrb_valid_address (res.value ()->pending (0).block_info (0).source ().c_str ()));

	// Rerun with too-large threshold
	pending.mutable_threshold ()->set_value ("200");
	res = handler.request (pending);
	ASSERT_EQ (0, res.value ()->pending (0).block_info_size ());
}
