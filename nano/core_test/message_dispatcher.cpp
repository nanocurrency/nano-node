#include <nano/lib/blocks.hpp>
#include <nano/lib/stats.hpp>
#include <nano/messages/asc_pull.hpp>
#include <nano/messages/confirm.hpp>
#include <nano/messages/telemetry.hpp>
#include <nano/node/node.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/vote_replier.hpp>
#include <nano/node/wallet.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

/*
 * A confirmation request the vote replier has no room for is dropped and counted by its message type, next to the replier's own overfill.
 */
TEST (message_dispatcher, confirm_req_refused)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.enable_voting = true;
	config.vote_replier->threads = 0; // Nothing takes requests off the queue
	config.vote_replier->channel_limit = 1;
	nano::node_flags flags;
	flags.disable_rep_crawler = true; // The crawler queries the local representative with confirmation requests too
	auto & node = *system.add_node (config, flags);
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	node.wallets.refresh_reps ();
	ASSERT_TRUE (node.is_voting ());

	auto channel = nano::test::fake_channel (node);
	nano::messages::confirm_req message{ nano::dev::network_params.network, nano::dev::genesis->hash (), nano::dev::genesis->root () };

	// The first request fills the channel's queue and is no drop
	node.inbound (message, channel);
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_replier, nano::stat::detail::request));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::message_drop, nano::stat::detail::confirm_req, nano::stat::dir::in));

	// The replier refuses the second, so it is dropped
	node.inbound (message, channel);
	ASSERT_EQ (1, node.stats.count (nano::stat::type::vote_replier, nano::stat::detail::overfill));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::message_drop, nano::stat::detail::confirm_req, nano::stat::dir::in));
}

/*
 * An ascending pull request the bootstrap server rejects is dropped and counted by its message type, next to the server's own reason.
 */
TEST (message_dispatcher, asc_pull_req_refused)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	auto make_request = [&node] (uint8_t count) {
		nano::messages::asc_pull_req request{ node.network_params.network };
		request.type = nano::messages::asc_pull_type::blocks;

		nano::messages::asc_pull_req::blocks_payload payload{};
		payload.start = nano::dev::genesis_key.pub;
		payload.count = count;
		payload.start_type = nano::messages::asc_pull_req::hash_type::account;
		request.payload = payload;
		request.update_header ();
		return request;
	};

	// The server takes a valid request, which is no drop
	node.inbound (make_request (1), nano::test::fake_channel (node));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::bootstrap_server, nano::stat::detail::request));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::message_drop, nano::stat::detail::asc_pull_req, nano::stat::dir::in));

	// A request for no blocks is rejected as invalid, so it is dropped
	node.inbound (make_request (0), nano::test::fake_channel (node));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::bootstrap_server, nano::stat::detail::invalid));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::message_drop, nano::stat::detail::asc_pull_req, nano::stat::dir::in));
}

/*
 * A confirmation request reaching a node that does not vote is dropped under a detail of its own, before the vote replier sees it.
 */
TEST (message_dispatcher, confirm_req_not_voting)
{
	nano::test::system system;
	nano::node_flags flags;
	flags.disable_rep_crawler = true; // The crawler queries the local representative with confirmation requests too
	auto & node = *system.add_node (flags);
	ASSERT_FALSE (node.is_voting ());

	nano::messages::confirm_req message{ nano::dev::network_params.network, nano::dev::genesis->hash (), nano::dev::genesis->root () };
	node.inbound (message, nano::test::fake_channel (node));

	ASSERT_EQ (1, node.stats.count (nano::stat::type::message_drop, nano::stat::detail::confirm_req_not_voting, nano::stat::dir::in));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::message_drop, nano::stat::detail::confirm_req, nano::stat::dir::in));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::vote_replier, nano::stat::detail::request));
}

/*
 * A confirmation request without roots asks for nothing and is dropped under a detail of its own, even on a voting node.
 */
TEST (message_dispatcher, confirm_req_empty)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.enable_voting = true;
	nano::node_flags flags;
	flags.disable_rep_crawler = true; // The crawler queries the local representative with confirmation requests too
	auto & node = *system.add_node (config, flags);
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	node.wallets.refresh_reps ();
	ASSERT_TRUE (node.is_voting ());

	// The deserializer rejects a request without roots, so this one is built and then emptied
	nano::messages::confirm_req message{ nano::dev::network_params.network, nano::dev::genesis->hash (), nano::dev::genesis->root () };
	message.roots_hashes.clear ();
	node.inbound (message, nano::test::fake_channel (node));

	ASSERT_EQ (1, node.stats.count (nano::stat::type::message_drop, nano::stat::detail::confirm_req_empty, nano::stat::dir::in));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::message_drop, nano::stat::detail::confirm_req, nano::stat::dir::in));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::vote_replier, nano::stat::detail::request));
}

/*
 * An ascending pull response that answers no request of this node is dropped and counted by its message type, next to bootstrap's missing tag.
 */
TEST (message_dispatcher, asc_pull_ack_unanswered)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	// A well formed blocks response under an id this node never handed out
	nano::messages::asc_pull_ack response{ node.network_params.network };
	response.id = 12345;
	response.type = nano::messages::asc_pull_type::blocks;
	response.payload = nano::messages::asc_pull_ack::blocks_payload{};
	response.update_header ();

	node.inbound (response, nano::test::fake_channel (node));

	ASSERT_EQ (1, node.stats.count (nano::stat::type::bootstrap, nano::stat::detail::missing_tag));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::message_drop, nano::stat::detail::asc_pull_ack, nano::stat::dir::in));
}

/*
 * A telemetry ack that fails verification is dropped and counted by its message type, next to telemetry's own reason.
 */
TEST (message_dispatcher, telemetry_ack_unverified)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	auto const message = nano::messages::telemetry_ack{ nano::dev::network_params.network, node.local_telemetry () };

	// Telemetry takes the node's own signed data over a channel carrying its node id, which is no drop
	node.inbound (message, nano::test::fake_channel (node));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::telemetry, nano::stat::detail::process));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::message_drop, nano::stat::detail::telemetry_ack, nano::stat::dir::in));

	// The same data over a channel of another node id fails verification, so it is dropped
	node.inbound (message, nano::test::fake_channel (node, /* node id */ { 123 }));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::telemetry, nano::stat::detail::node_id_mismatch));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::message_drop, nano::stat::detail::telemetry_ack, nano::stat::dir::in));
}
