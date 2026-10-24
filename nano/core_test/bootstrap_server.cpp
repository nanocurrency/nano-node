#include <nano/lib/blocks.hpp>
#include <nano/messages/asc_pull.hpp>
#include <nano/node/bootstrap/bootstrap_server.hpp>
#include <nano/node/node.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/transport/fake.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/store/ledger/topology.hpp>
#include <nano/store/ledger_store.hpp>
#include <nano/test_common/chains.hpp>
#include <nano/test_common/random.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <iterator>
#include <map>

using namespace std::chrono_literals;

namespace
{
class responses_helper final
{
public:
	void add (nano::messages::asc_pull_ack const & ack)
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		responses.push_back (ack);
	}

	std::vector<nano::messages::asc_pull_ack> get ()
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		return responses;
	}

	std::size_t size ()
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		return responses.size ();
	}

	void connect (nano::bootstrap_server & server)
	{
		server.on_response.add ([&] (auto & response, auto & channel) {
			add (response);
		});
	}

private:
	nano::mutex mutex;
	std::vector<nano::messages::asc_pull_ack> responses;
};

/**
 * Checks if both lists contain the same blocks, with `blocks_b` skipped by `skip` elements
 */
bool compare_blocks (auto const & blocks_a, auto const & blocks_b, int skip = 0)
{
	debug_assert (blocks_b.size () >= blocks_a.size () + skip);

	const auto count = blocks_a.size ();
	for (int n = 0; n < count; ++n)
	{
		auto & block_a = *blocks_a[n];
		auto & block_b = *blocks_b[n + skip];

		// nano::block does not have != operator
		if (!(block_a == block_b))
		{
			return false;
		}
	}
	return true;
}
}

TEST (bootstrap_server, serve_account_blocks)
{
	nano::test::system system{};
	auto & node = *system.add_node ();

	responses_helper responses;
	responses.connect (node.bootstrap_server);

	auto chains = nano::test::setup_chains (system, node, 1, 128);
	auto [first_account, first_blocks] = chains.front ();

	// Request blocks from account root
	nano::messages::asc_pull_req request{ node.network_params.network };
	request.id = 7;
	request.type = nano::messages::asc_pull_type::blocks;

	nano::messages::asc_pull_req::blocks_payload request_payload{};
	request_payload.start = first_account;
	request_payload.count = nano::bootstrap_server::max_blocks;
	request_payload.start_type = nano::messages::asc_pull_req::hash_type::account;

	request.payload = request_payload;
	request.update_header ();

	node.inbound (request, nano::test::fake_channel (node));

	ASSERT_TIMELY_EQ (5s, responses.size (), 1);

	auto response = responses.get ().front ();
	// Ensure we got response exactly for what we asked for
	ASSERT_EQ (response.id, 7);
	ASSERT_EQ (response.type, nano::messages::asc_pull_type::blocks);

	nano::messages::asc_pull_ack::blocks_payload response_payload;
	ASSERT_NO_THROW (response_payload = std::get<nano::messages::asc_pull_ack::blocks_payload> (response.payload));
	ASSERT_EQ (response_payload.blocks.size (), 128);
	ASSERT_TRUE (compare_blocks (response_payload.blocks, first_blocks));

	// Ensure we don't get any unexpected responses
	ASSERT_ALWAYS (1s, responses.size () == 1);
}

TEST (bootstrap_server, serve_hash)
{
	nano::test::system system{};
	auto & node = *system.add_node ();

	responses_helper responses;
	responses.connect (node.bootstrap_server);

	auto chains = nano::test::setup_chains (system, node, 1, 256);
	auto [account, blocks] = chains.front ();

	// Skip a few blocks to request hash in the middle of the chain
	blocks = nano::block_list_t{ std::next (blocks.begin (), 9), blocks.end () };

	// Request blocks from the middle of the chain
	nano::messages::asc_pull_req request{ node.network_params.network };
	request.id = 7;
	request.type = nano::messages::asc_pull_type::blocks;

	nano::messages::asc_pull_req::blocks_payload request_payload{};
	request_payload.start = blocks.front ()->hash ();
	request_payload.count = nano::bootstrap_server::max_blocks;
	request_payload.start_type = nano::messages::asc_pull_req::hash_type::block;

	request.payload = request_payload;
	request.update_header ();

	node.inbound (request, nano::test::fake_channel (node));

	ASSERT_TIMELY_EQ (5s, responses.size (), 1);

	auto response = responses.get ().front ();
	// Ensure we got response exactly for what we asked for
	ASSERT_EQ (response.id, 7);
	ASSERT_EQ (response.type, nano::messages::asc_pull_type::blocks);

	nano::messages::asc_pull_ack::blocks_payload response_payload;
	ASSERT_NO_THROW (response_payload = std::get<nano::messages::asc_pull_ack::blocks_payload> (response.payload));
	ASSERT_EQ (response_payload.blocks.size (), 128);
	ASSERT_TRUE (compare_blocks (response_payload.blocks, blocks));

	// Ensure we don't get any unexpected responses
	ASSERT_ALWAYS (1s, responses.size () == 1);
}

TEST (bootstrap_server, serve_hash_one)
{
	nano::test::system system{};
	auto & node = *system.add_node ();

	responses_helper responses;
	responses.connect (node.bootstrap_server);

	auto chains = nano::test::setup_chains (system, node, 1, 256);
	auto [account, blocks] = chains.front ();

	// Skip a few blocks to request hash in the middle of the chain
	blocks = nano::block_list_t{ std::next (blocks.begin (), 9), blocks.end () };

	// Request blocks from the middle of the chain
	nano::messages::asc_pull_req request{ node.network_params.network };
	request.id = 7;
	request.type = nano::messages::asc_pull_type::blocks;

	nano::messages::asc_pull_req::blocks_payload request_payload{};
	request_payload.start = blocks.front ()->hash ();
	request_payload.count = 1;
	request_payload.start_type = nano::messages::asc_pull_req::hash_type::block;

	request.payload = request_payload;
	request.update_header ();

	node.inbound (request, nano::test::fake_channel (node));

	ASSERT_TIMELY_EQ (5s, responses.size (), 1);

	auto response = responses.get ().front ();
	// Ensure we got response exactly for what we asked for
	ASSERT_EQ (response.id, 7);
	ASSERT_EQ (response.type, nano::messages::asc_pull_type::blocks);

	nano::messages::asc_pull_ack::blocks_payload response_payload;
	ASSERT_NO_THROW (response_payload = std::get<nano::messages::asc_pull_ack::blocks_payload> (response.payload));
	ASSERT_EQ (response_payload.blocks.size (), 1);
	ASSERT_EQ (response_payload.blocks.front ()->hash (), request_payload.start.as_block_hash ());
}

TEST (bootstrap_server, serve_end_of_chain)
{
	nano::test::system system{};
	auto & node = *system.add_node ();

	responses_helper responses;
	responses.connect (node.bootstrap_server);

	auto chains = nano::test::setup_chains (system, node, 1, 128);
	auto [account, blocks] = chains.front ();

	// Request blocks from account frontier
	nano::messages::asc_pull_req request{ node.network_params.network };
	request.id = 7;
	request.type = nano::messages::asc_pull_type::blocks;

	nano::messages::asc_pull_req::blocks_payload request_payload{};
	request_payload.start = blocks.back ()->hash ();
	request_payload.count = nano::bootstrap_server::max_blocks;
	request_payload.start_type = nano::messages::asc_pull_req::hash_type::block;

	request.payload = request_payload;
	request.update_header ();

	node.inbound (request, nano::test::fake_channel (node));

	ASSERT_TIMELY_EQ (5s, responses.size (), 1);

	auto response = responses.get ().front ();
	// Ensure we got response exactly for what we asked for
	ASSERT_EQ (response.id, 7);
	ASSERT_EQ (response.type, nano::messages::asc_pull_type::blocks);

	nano::messages::asc_pull_ack::blocks_payload response_payload;
	ASSERT_NO_THROW (response_payload = std::get<nano::messages::asc_pull_ack::blocks_payload> (response.payload));
	// Response should contain only the last block from chain
	ASSERT_EQ (response_payload.blocks.size (), 1);
	ASSERT_EQ (*response_payload.blocks.front (), *blocks.back ());
}

TEST (bootstrap_server, serve_missing)
{
	nano::test::system system{};
	auto & node = *system.add_node ();

	responses_helper responses;
	responses.connect (node.bootstrap_server);

	auto chains = nano::test::setup_chains (system, node, 1, 128);

	// Request blocks from account frontier
	nano::messages::asc_pull_req request{ node.network_params.network };
	request.id = 7;
	request.type = nano::messages::asc_pull_type::blocks;

	nano::messages::asc_pull_req::blocks_payload request_payload{};
	request_payload.start = nano::test::random_hash ();
	request_payload.count = nano::bootstrap_server::max_blocks;
	request_payload.start_type = nano::messages::asc_pull_req::hash_type::block;

	request.payload = request_payload;
	request.update_header ();

	node.inbound (request, nano::test::fake_channel (node));

	ASSERT_TIMELY_EQ (5s, responses.size (), 1);

	auto response = responses.get ().front ();
	// Ensure we got response exactly for what we asked for
	ASSERT_EQ (response.id, 7);
	ASSERT_EQ (response.type, nano::messages::asc_pull_type::blocks);

	nano::messages::asc_pull_ack::blocks_payload response_payload;
	ASSERT_NO_THROW (response_payload = std::get<nano::messages::asc_pull_ack::blocks_payload> (response.payload));
	// There should be nothing sent
	ASSERT_EQ (response_payload.blocks.size (), 0);
}

TEST (bootstrap_server, serve_multiple)
{
	nano::test::system system{};
	auto & node = *system.add_node ();

	responses_helper responses;
	responses.connect (node.bootstrap_server);

	auto chains = nano::test::setup_chains (system, node, 32, 16);

	{
		// Request blocks from multiple chains at once
		int next_id = 0;
		for (auto & [account, blocks] : chains)
		{
			// Request blocks from account root
			nano::messages::asc_pull_req request{ node.network_params.network };
			request.id = next_id++;
			request.type = nano::messages::asc_pull_type::blocks;

			nano::messages::asc_pull_req::blocks_payload request_payload{};
			request_payload.start = account;
			request_payload.count = nano::bootstrap_server::max_blocks;
			request_payload.start_type = nano::messages::asc_pull_req::hash_type::account;

			request.payload = request_payload;
			request.update_header ();

			node.inbound (request, nano::test::fake_channel (node));
		}
	}

	ASSERT_TIMELY_EQ (15s, responses.size (), chains.size ());

	auto all_responses = responses.get ();
	{
		int next_id = 0;
		for (auto & [account, blocks] : chains)
		{
			// Find matching response
			auto response_it = std::find_if (all_responses.begin (), all_responses.end (), [&] (auto ack) { return ack.id == next_id; });
			ASSERT_TRUE (response_it != all_responses.end ());
			auto response = *response_it;

			// Ensure we got response exactly for what we asked for
			ASSERT_EQ (response.id, next_id);
			ASSERT_EQ (response.type, nano::messages::asc_pull_type::blocks);

			nano::messages::asc_pull_ack::blocks_payload response_payload;
			ASSERT_NO_THROW (response_payload = std::get<nano::messages::asc_pull_ack::blocks_payload> (response.payload));
			ASSERT_EQ (response_payload.blocks.size (), 17); // 1 open block + 16 random blocks
			ASSERT_TRUE (compare_blocks (response_payload.blocks, blocks));

			++next_id;
		}
	}

	// Ensure we don't get any unexpected responses
	ASSERT_ALWAYS (1s, responses.size () == chains.size ());
}

TEST (bootstrap_server, serve_account_info)
{
	nano::test::system system{};
	auto & node = *system.add_node ();

	responses_helper responses;
	responses.connect (node.bootstrap_server);

	auto chains = nano::test::setup_chains (system, node, 1, 128);
	auto [account, blocks] = chains.front ();

	// Request blocks from account root
	nano::messages::asc_pull_req request{ node.network_params.network };
	request.id = 7;
	request.type = nano::messages::asc_pull_type::account_info;

	nano::messages::asc_pull_req::account_info_payload request_payload{};
	request_payload.target = account;
	request_payload.target_type = nano::messages::asc_pull_req::hash_type::account;

	request.payload = request_payload;
	request.update_header ();

	node.inbound (request, nano::test::fake_channel (node));

	ASSERT_TIMELY_EQ (5s, responses.size (), 1);

	auto response = responses.get ().front ();
	// Ensure we got response exactly for what we asked for
	ASSERT_EQ (response.id, 7);
	ASSERT_EQ (response.type, nano::messages::asc_pull_type::account_info);

	nano::messages::asc_pull_ack::account_info_payload response_payload;
	ASSERT_NO_THROW (response_payload = std::get<nano::messages::asc_pull_ack::account_info_payload> (response.payload));

	ASSERT_EQ (response_payload.account, account);
	ASSERT_EQ (response_payload.account_open, blocks.front ()->hash ());
	ASSERT_EQ (response_payload.account_head, blocks.back ()->hash ());
	ASSERT_EQ (response_payload.account_block_count, blocks.size ());
	ASSERT_EQ (response_payload.account_conf_frontier, blocks.back ()->hash ());
	ASSERT_EQ (response_payload.account_conf_height, blocks.size ());

	// Ensure we don't get any unexpected responses
	ASSERT_ALWAYS (1s, responses.size () == 1);
}

TEST (bootstrap_server, serve_account_info_missing)
{
	nano::test::system system{};
	auto & node = *system.add_node ();

	responses_helper responses;
	responses.connect (node.bootstrap_server);

	auto chains = nano::test::setup_chains (system, node, 1, 128);
	auto [account, blocks] = chains.front ();

	// Request blocks from account root
	nano::messages::asc_pull_req request{ node.network_params.network };
	request.id = 7;
	request.type = nano::messages::asc_pull_type::account_info;

	nano::messages::asc_pull_req::account_info_payload request_payload{};
	request_payload.target = nano::test::random_account ();
	request_payload.target_type = nano::messages::asc_pull_req::hash_type::account;

	request.payload = request_payload;
	request.update_header ();

	node.inbound (request, nano::test::fake_channel (node));

	ASSERT_TIMELY_EQ (5s, responses.size (), 1);

	auto response = responses.get ().front ();
	// Ensure we got response exactly for what we asked for
	ASSERT_EQ (response.id, 7);
	ASSERT_EQ (response.type, nano::messages::asc_pull_type::account_info);

	nano::messages::asc_pull_ack::account_info_payload response_payload;
	ASSERT_NO_THROW (response_payload = std::get<nano::messages::asc_pull_ack::account_info_payload> (response.payload));

	ASSERT_EQ (response_payload.account, request_payload.target.as_account ());
	ASSERT_EQ (response_payload.account_open, 0);
	ASSERT_EQ (response_payload.account_head, 0);
	ASSERT_EQ (response_payload.account_block_count, 0);
	ASSERT_EQ (response_payload.account_conf_frontier, 0);
	ASSERT_EQ (response_payload.account_conf_height, 0);

	// Ensure we don't get any unexpected responses
	ASSERT_ALWAYS (1s, responses.size () == 1);
}

TEST (bootstrap_server, serve_frontiers)
{
	nano::test::system system{};
	auto & node = *system.add_node ();

	responses_helper responses;
	responses.connect (node.bootstrap_server);

	auto chains = nano::test::setup_chains (system, node, /* chain count */ 32, /* block count */ 4);

	// Request all frontiers
	nano::messages::asc_pull_req request{ node.network_params.network };
	request.id = 7;
	request.type = nano::messages::asc_pull_type::frontiers;

	nano::messages::asc_pull_req::frontiers_payload request_payload{};
	request_payload.count = nano::bootstrap_server::max_frontiers;
	request_payload.start = 0;

	request.payload = request_payload;
	request.update_header ();

	node.inbound (request, nano::test::fake_channel (node));

	ASSERT_TIMELY_EQ (5s, responses.size (), 1);

	auto response = responses.get ().front ();
	// Ensure we got response exactly for what we asked for
	ASSERT_EQ (response.id, 7);
	ASSERT_EQ (response.type, nano::messages::asc_pull_type::frontiers);

	nano::messages::asc_pull_ack::frontiers_payload response_payload;
	ASSERT_NO_THROW (response_payload = std::get<nano::messages::asc_pull_ack::frontiers_payload> (response.payload));

	ASSERT_EQ (response_payload.frontiers.size (), chains.size () + 1); // +1 for genesis

	// Ensure frontiers match what we expect
	std::map<nano::account, nano::block_hash> expected_frontiers;
	for (auto & [account, blocks] : chains)
	{
		expected_frontiers[account] = blocks.back ()->hash ();
	}
	expected_frontiers[nano::dev::genesis_key.pub] = node.latest (nano::dev::genesis_key.pub);

	for (auto & [account, frontier] : response_payload.frontiers)
	{
		ASSERT_EQ (frontier, expected_frontiers[account]);
		expected_frontiers.erase (account);
	}
	ASSERT_TRUE (expected_frontiers.empty ());
}

TEST (bootstrap_server, serve_frontiers_invalid_count)
{
	nano::test::system system{};
	auto & node = *system.add_node ();

	responses_helper responses;
	responses.connect (node.bootstrap_server);

	auto chains = nano::test::setup_chains (system, node, /* chain count */ 4, /* block count */ 4);

	// Zero count
	{
		nano::messages::asc_pull_req request{ node.network_params.network };
		request.id = 7;
		request.type = nano::messages::asc_pull_type::frontiers;

		nano::messages::asc_pull_req::frontiers_payload request_payload{};
		request_payload.count = 0;
		request_payload.start = 0;

		request.payload = request_payload;
		request.update_header ();

		node.inbound (request, nano::test::fake_channel (node));
	}

	ASSERT_TIMELY_EQ (5s, node.stats.count (nano::stat::type::bootstrap_server, nano::stat::detail::invalid), 1);

	// Count larger than allowed
	{
		nano::messages::asc_pull_req request{ node.network_params.network };
		request.id = 7;
		request.type = nano::messages::asc_pull_type::frontiers;

		nano::messages::asc_pull_req::frontiers_payload request_payload{};
		request_payload.count = nano::bootstrap_server::max_frontiers + 1;
		request_payload.start = 0;

		request.payload = request_payload;
		request.update_header ();

		node.inbound (request, nano::test::fake_channel (node));
	}

	ASSERT_TIMELY_EQ (5s, node.stats.count (nano::stat::type::bootstrap_server, nano::stat::detail::invalid), 2);

	// Max numeric value
	{
		nano::messages::asc_pull_req request{ node.network_params.network };
		request.id = 7;
		request.type = nano::messages::asc_pull_type::frontiers;

		nano::messages::asc_pull_req::frontiers_payload request_payload{};
		request_payload.count = std::numeric_limits<decltype (request_payload.count)>::max ();
		request_payload.start = 0;

		request.payload = request_payload;
		request.update_header ();

		node.inbound (request, nano::test::fake_channel (node));
	}

	ASSERT_TIMELY_EQ (5s, node.stats.count (nano::stat::type::bootstrap_server, nano::stat::detail::invalid), 3);

	// Ensure we don't get any unexpected responses
	ASSERT_ALWAYS (1s, responses.size () == 0);
}

TEST (bootstrap_server, serve_blocks_random)
{
	nano::test::system system{};
	auto & node = *system.add_node ();

	responses_helper responses;
	responses.connect (node.bootstrap_server);

	auto chains = nano::test::setup_chains (system, node, /* chain count */ 1, /* block count */ 16);
	auto [account, blocks] = chains.front ();

	// Mix of existing and non-existing hashes
	std::deque<nano::block_hash> requested;
	requested.push_back (blocks[0]->hash ());
	requested.push_back (nano::test::random_hash ()); // missing
	requested.push_back (blocks[5]->hash ());
	requested.push_back (blocks[10]->hash ());
	requested.push_back (nano::test::random_hash ()); // missing

	nano::messages::asc_pull_req request{ node.network_params.network };
	request.id = 7;
	request.type = nano::messages::asc_pull_type::blocks_random;

	nano::messages::asc_pull_req::blocks_random_payload payload{};
	payload.hashes = requested;
	request.payload = payload;
	request.update_header ();

	node.inbound (request, nano::test::fake_channel (node));

	ASSERT_TIMELY_EQ (5s, responses.size (), 1);

	auto response = responses.get ().front ();
	ASSERT_EQ (response.id, 7);
	ASSERT_EQ (response.type, nano::messages::asc_pull_type::blocks_random);

	auto response_payload = std::get<nano::messages::asc_pull_ack::blocks_payload> (response.payload);
	ASSERT_EQ (response_payload.blocks.size (), 3);

	std::set<nano::block_hash> returned;
	for (auto const & b : response_payload.blocks)
	{
		returned.insert (b->hash ());
	}
	ASSERT_TRUE (returned.count (blocks[0]->hash ()));
	ASSERT_TRUE (returned.count (blocks[5]->hash ()));
	ASSERT_TRUE (returned.count (blocks[10]->hash ()));
}

TEST (bootstrap_server, serve_blocks_random_empty)
{
	nano::test::system system{};
	auto & node = *system.add_node ();

	responses_helper responses;
	responses.connect (node.bootstrap_server);

	nano::test::setup_chains (system, node, 1, 8);

	std::deque<nano::block_hash> requested;
	requested.push_back (nano::test::random_hash ());
	requested.push_back (nano::test::random_hash ());

	nano::messages::asc_pull_req request{ node.network_params.network };
	request.id = 7;
	request.type = nano::messages::asc_pull_type::blocks_random;

	nano::messages::asc_pull_req::blocks_random_payload payload{};
	payload.hashes = requested;
	request.payload = payload;
	request.update_header ();

	node.inbound (request, nano::test::fake_channel (node));

	ASSERT_TIMELY_EQ (5s, responses.size (), 1);

	auto response_payload = std::get<nano::messages::asc_pull_ack::blocks_payload> (responses.get ().front ().payload);
	ASSERT_TRUE (response_payload.blocks.empty ());
}

TEST (bootstrap_server, serve_blocks_random_invalid)
{
	nano::test::system system{};
	auto & node = *system.add_node ();

	responses_helper responses;
	responses.connect (node.bootstrap_server);

	// Empty hash list - rejected by verify
	{
		nano::messages::asc_pull_req request{ node.network_params.network };
		request.id = 7;
		request.type = nano::messages::asc_pull_type::blocks_random;

		nano::messages::asc_pull_req::blocks_random_payload payload{}; // empty
		request.payload = payload;
		request.update_header ();

		node.inbound (request, nano::test::fake_channel (node));
	}

	ASSERT_TIMELY_EQ (5s, node.stats.count (nano::stat::type::bootstrap_server, nano::stat::detail::invalid), 1);
	ASSERT_ALWAYS (1s, responses.size () == 0);
}

TEST (bootstrap_server, serve_topo_index)
{
	nano::test::system system{};
	auto & node = *system.add_node ();

	responses_helper responses;
	responses.connect (node.bootstrap_server);

	nano::test::setup_chains (system, node, /* chain count */ 4, /* block count */ 4);

	auto entry_count = [&] () {
		auto txn = node.ledger.tx_begin_read ();
		return node.ledger.store.topology.count (txn);
	};
	ASSERT_GT (entry_count (), 0u);

	// Request from beginning
	nano::messages::asc_pull_req request{ node.network_params.network };
	request.id = 7;
	request.type = nano::messages::asc_pull_type::topo_index;

	nano::messages::asc_pull_req::topo_index_payload payload{};
	payload.start = nano::topo_key{};
	payload.count = 1000;
	request.payload = payload;
	request.update_header ();

	node.inbound (request, nano::test::fake_channel (node));

	ASSERT_TIMELY_EQ (5s, responses.size (), 1);

	auto response = responses.get ().front ();
	ASSERT_EQ (response.id, 7);
	ASSERT_EQ (response.type, nano::messages::asc_pull_type::topo_index);

	auto response_payload = std::get<nano::messages::asc_pull_ack::topo_index_payload> (response.payload);
	ASSERT_FALSE (response_payload.entries.empty ());

	// Verify entries are in topo-ascending order AND dense: topo heights are
	// contiguous, so consecutive entries are at most one height apart.
	for (std::size_t n = 1; n < response_payload.entries.size (); ++n)
	{
		ASSERT_LT (response_payload.entries[n - 1], response_payload.entries[n]);
		ASSERT_LE (response_payload.entries[n].topo_height - response_payload.entries[n - 1].topo_height, 1u);
	}

	// Continuation request from last returned entry
	auto last = response_payload.entries.back ();

	nano::messages::asc_pull_req request2{ node.network_params.network };
	request2.id = 8;
	request2.type = nano::messages::asc_pull_type::topo_index;

	nano::messages::asc_pull_req::topo_index_payload payload2{};
	payload2.start = last;
	payload2.count = 1000;
	request2.payload = payload2;
	request2.update_header ();

	node.inbound (request2, nano::test::fake_channel (node));

	ASSERT_TIMELY_EQ (5s, responses.size (), 2);

	auto response2 = responses.get ().back ();
	auto response2_payload = std::get<nano::messages::asc_pull_ack::topo_index_payload> (response2.payload);

	// Continuation should include `last` at the start (lower_bound semantics)
	if (!response2_payload.entries.empty ())
	{
		ASSERT_GE (response2_payload.entries.front (), last);
	}
}

TEST (bootstrap_server, serve_topo_index_empty)
{
	nano::test::system system{};
	auto & node = *system.add_node ();

	responses_helper responses;
	responses.connect (node.bootstrap_server);

	nano::test::setup_chains (system, node, 1, 4);

	// Request from a position past the end
	nano::messages::asc_pull_req request{ node.network_params.network };
	request.id = 7;
	request.type = nano::messages::asc_pull_type::topo_index;

	nano::messages::asc_pull_req::topo_index_payload payload{};
	payload.start = nano::topo_key{ std::numeric_limits<uint64_t>::max () - 1, nano::block_hash{} };
	payload.count = 100;
	request.payload = payload;
	request.update_header ();

	node.inbound (request, nano::test::fake_channel (node));

	ASSERT_TIMELY_EQ (5s, responses.size (), 1);

	auto response_payload = std::get<nano::messages::asc_pull_ack::topo_index_payload> (responses.get ().front ().payload);
	ASSERT_TRUE (response_payload.entries.empty ());
}

TEST (bootstrap_server, serve_topo_index_invalid_count)
{
	nano::test::system system{};
	auto & node = *system.add_node ();

	responses_helper responses;
	responses.connect (node.bootstrap_server);

	nano::test::setup_chains (system, node, 1, 4);

	// Zero count
	{
		nano::messages::asc_pull_req request{ node.network_params.network };
		request.id = 7;
		request.type = nano::messages::asc_pull_type::topo_index;

		nano::messages::asc_pull_req::topo_index_payload payload{};
		payload.count = 0;
		request.payload = payload;
		request.update_header ();

		node.inbound (request, nano::test::fake_channel (node));
	}

	ASSERT_TIMELY_EQ (5s, node.stats.count (nano::stat::type::bootstrap_server, nano::stat::detail::invalid), 1);

	// Count over max
	{
		nano::messages::asc_pull_req request{ node.network_params.network };
		request.id = 7;
		request.type = nano::messages::asc_pull_type::topo_index;

		nano::messages::asc_pull_req::topo_index_payload payload{};
		payload.count = nano::bootstrap_server::max_topo_entries + 1;
		request.payload = payload;
		request.update_header ();

		node.inbound (request, nano::test::fake_channel (node));
	}

	ASSERT_TIMELY_EQ (5s, node.stats.count (nano::stat::type::bootstrap_server, nano::stat::detail::invalid), 2);
	ASSERT_ALWAYS (1s, responses.size () == 0);
}

/*
 * A blocks random response carries at most max_random_blocks even when the request names more blocks than that and the ledger holds them all.
 * The request is filled to its hash capacity with unknown hashes first and the blocks of one chain after them, so unknown hashes would use up the cap first if they counted towards it.
 * The response must hold exactly the first max_random_blocks chain blocks in request order, leaving the tail of the chain for a retry.
 */
TEST (bootstrap_server, serve_blocks_random_limit)
{
	nano::test::system system{};
	auto & node = *system.add_node ();

	responses_helper responses;
	responses.connect (node.bootstrap_server);

	// One chain holding more blocks than a blocks random response may carry
	auto chains = nano::test::setup_chains (system, node, /* chain count */ 1, /* block count */ 110);
	auto [account, blocks] = chains.front ();
	ASSERT_GT (blocks.size (), nano::bootstrap_server::max_random_blocks);
	ASSERT_LT (blocks.size (), nano::bootstrap_server::max_random_hashes);

	// Fill the request to capacity, unknown hashes first so they would use up the limit if they counted towards it
	std::deque<nano::block_hash> requested;
	while (requested.size () < nano::bootstrap_server::max_random_hashes - blocks.size ())
	{
		requested.push_back (nano::test::random_hash ());
	}
	for (auto const & block : blocks)
	{
		requested.push_back (block->hash ());
	}

	nano::messages::asc_pull_req request{ node.network_params.network };
	request.id = 7;
	request.type = nano::messages::asc_pull_type::blocks_random;

	nano::messages::asc_pull_req::blocks_random_payload payload{};
	payload.hashes = requested;
	request.payload = payload;
	request.update_header ();

	node.inbound (request, nano::test::fake_channel (node));

	ASSERT_TIMELY_EQ (5s, responses.size (), 1);

	// The response stops at the limit and holds the first known blocks in request order, dropping the tail of the chain
	auto response_payload = std::get<nano::messages::asc_pull_ack::blocks_payload> (responses.get ().front ().payload);
	ASSERT_EQ (response_payload.blocks.size (), nano::bootstrap_server::max_random_blocks);
	for (std::size_t n = 0; n < response_payload.blocks.size (); ++n)
	{
		ASSERT_EQ (response_payload.blocks[n]->hash (), blocks[n]->hash ());
	}
}

/*
 * A blocks request is dropped before it reaches the ledger when its count is zero or above what one response can carry.
 * The requested account exists, so the count is the only reason for the drop; each rejected request counts as invalid and nothing is sent back.
 */
TEST (bootstrap_server, serve_blocks_invalid_count)
{
	nano::test::system system{};
	auto & node = *system.add_node ();

	responses_helper responses;
	responses.connect (node.bootstrap_server);

	auto chains = nano::test::setup_chains (system, node, /* chain count */ 1, /* block count */ 4);
	auto [account, blocks] = chains.front ();

	// A count of zero asks for nothing and is rejected
	{
		nano::messages::asc_pull_req request{ node.network_params.network };
		request.id = 7;
		request.type = nano::messages::asc_pull_type::blocks;

		nano::messages::asc_pull_req::blocks_payload request_payload{};
		request_payload.start = account;
		request_payload.count = 0;
		request_payload.start_type = nano::messages::asc_pull_req::hash_type::account;
		request.payload = request_payload;
		request.update_header ();

		node.inbound (request, nano::test::fake_channel (node));
	}

	ASSERT_TIMELY_EQ (5s, node.stats.count (nano::stat::type::bootstrap_server, nano::stat::detail::invalid), 1);

	// A count above the response capacity is rejected instead of being clamped
	{
		nano::messages::asc_pull_req request{ node.network_params.network };
		request.id = 8;
		request.type = nano::messages::asc_pull_type::blocks;

		nano::messages::asc_pull_req::blocks_payload request_payload{};
		request_payload.start = account;
		request_payload.count = nano::bootstrap_server::max_blocks + 1;
		request_payload.start_type = nano::messages::asc_pull_req::hash_type::account;
		request.payload = request_payload;
		request.update_header ();

		node.inbound (request, nano::test::fake_channel (node));
	}

	ASSERT_TIMELY_EQ (5s, node.stats.count (nano::stat::type::bootstrap_server, nano::stat::detail::invalid), 2);

	// Neither request produced a response
	ASSERT_ALWAYS (1s, responses.size () == 0);
}

/*
 * An account info request whose target is the zero account or the zero hash is dropped, whichever target type it claims.
 * Zero is the null value of both and no account can be looked up from it; each rejected request counts as invalid and nothing is sent back.
 */
TEST (bootstrap_server, serve_account_info_invalid)
{
	nano::test::system system{};
	auto & node = *system.add_node ();

	responses_helper responses;
	responses.connect (node.bootstrap_server);

	// The target is left at its zero default and claimed to be an account
	{
		nano::messages::asc_pull_req request{ node.network_params.network };
		request.id = 7;
		request.type = nano::messages::asc_pull_type::account_info;

		nano::messages::asc_pull_req::account_info_payload request_payload{};
		request_payload.target_type = nano::messages::asc_pull_req::hash_type::account;
		request.payload = request_payload;
		request.update_header ();

		node.inbound (request, nano::test::fake_channel (node));
	}

	ASSERT_TIMELY_EQ (5s, node.stats.count (nano::stat::type::bootstrap_server, nano::stat::detail::invalid), 1);

	// The same zero target claimed to be a block hash is rejected as well
	{
		nano::messages::asc_pull_req request{ node.network_params.network };
		request.id = 8;
		request.type = nano::messages::asc_pull_type::account_info;

		nano::messages::asc_pull_req::account_info_payload request_payload{};
		request_payload.target_type = nano::messages::asc_pull_req::hash_type::block;
		request.payload = request_payload;
		request.update_header ();

		node.inbound (request, nano::test::fake_channel (node));
	}

	ASSERT_TIMELY_EQ (5s, node.stats.count (nano::stat::type::bootstrap_server, nano::stat::detail::invalid), 2);

	// Neither request produced a response
	ASSERT_ALWAYS (1s, responses.size () == 0);
}

/*
 * A frontiers response stops at the requested count even though the ledger holds more accounts.
 * The ledger holds the accounts of 32 chains and genesis and 8 frontiers are requested from the start of the account space.
 * The response must hold exactly the 8 lowest accounts in ascending order, each with its head block, leaving the rest for a continuation request.
 */
TEST (bootstrap_server, serve_frontiers_limit)
{
	nano::test::system system{};
	auto & node = *system.add_node ();

	responses_helper responses;
	responses.connect (node.bootstrap_server);

	auto chains = nano::test::setup_chains (system, node, /* chain count */ 32, /* block count */ 4);
	std::size_t const count = 8;

	// Every frontier the ledger holds, ordered by account, so the lowest `count` of them are known before the request
	std::map<nano::account, nano::block_hash> frontiers;
	for (auto const & [account, blocks] : chains)
	{
		frontiers[account] = blocks.back ()->hash ();
	}
	frontiers[nano::dev::genesis_key.pub] = node.latest (nano::dev::genesis_key.pub);
	ASSERT_GT (frontiers.size (), count);

	// Request `count` frontiers from the start of the account space
	nano::messages::asc_pull_req request{ node.network_params.network };
	request.id = 7;
	request.type = nano::messages::asc_pull_type::frontiers;

	nano::messages::asc_pull_req::frontiers_payload request_payload{};
	request_payload.start = 0;
	request_payload.count = count;
	request.payload = request_payload;
	request.update_header ();

	node.inbound (request, nano::test::fake_channel (node));

	ASSERT_TIMELY_EQ (5s, responses.size (), 1);

	// Exactly `count` frontiers come back: the lowest accounts in ascending order, each with its head
	auto response_payload = std::get<nano::messages::asc_pull_ack::frontiers_payload> (responses.get ().front ().payload);
	ASSERT_EQ (response_payload.frontiers.size (), count);
	auto expected = frontiers.begin ();
	for (auto const & [account, frontier] : response_payload.frontiers)
	{
		ASSERT_EQ (account, expected->first);
		ASSERT_EQ (frontier, expected->second);
		++expected;
	}
}

/*
 * A topo index response stops at the requested count even though the topology store holds more entries.
 * The store holds the entries of 4 chains with 4 blocks each and 5 entries are requested from the lowest key.
 * The response must hold exactly the 5 lowest keys in store order, leaving the rest for a continuation request.
 */
TEST (bootstrap_server, serve_topo_index_limit)
{
	nano::test::system system{};
	auto & node = *system.add_node ();

	responses_helper responses;
	responses.connect (node.bootstrap_server);

	nano::test::setup_chains (system, node, /* chain count */ 4, /* block count */ 4);
	std::size_t const count = 5;

	// The lowest `count` keys read straight from the store are what the response must contain
	std::deque<nano::topo_key> expected;
	{
		auto transaction = node.ledger.tx_begin_read ();
		ASSERT_GT (node.ledger.store.topology.count (transaction), count);
		for (auto it = node.ledger.store.topology.begin (transaction), end = node.ledger.store.topology.end (transaction); it != end && expected.size () < count; ++it)
		{
			expected.push_back (it->first);
		}
	}

	// Request `count` entries from the lowest key
	nano::messages::asc_pull_req request{ node.network_params.network };
	request.id = 7;
	request.type = nano::messages::asc_pull_type::topo_index;

	nano::messages::asc_pull_req::topo_index_payload request_payload{};
	request_payload.start = nano::topo_key{};
	request_payload.count = count;
	request.payload = request_payload;
	request.update_header ();

	node.inbound (request, nano::test::fake_channel (node));

	ASSERT_TIMELY_EQ (5s, responses.size (), 1);

	// Exactly `count` entries come back, the lowest keys in store order
	auto response_payload = std::get<nano::messages::asc_pull_ack::topo_index_payload> (responses.get ().front ().payload);
	ASSERT_EQ (response_payload.entries.size (), count);
	for (std::size_t n = 0; n < count; ++n)
	{
		ASSERT_EQ (response_payload.entries[n], expected[n]);
	}
}

/*
 * A blocks request by an unknown account is answered with an empty blocks response rather than dropped.
 * The count is valid, so the account lookup is the only thing that can fail; the empty response tells the requester the account does not exist here.
 */
TEST (bootstrap_server, serve_missing_account)
{
	nano::test::system system{};
	auto & node = *system.add_node ();

	responses_helper responses;
	responses.connect (node.bootstrap_server);

	nano::test::setup_chains (system, node, /* chain count */ 1, /* block count */ 4);

	// Request the chain of an account the ledger has never seen
	nano::messages::asc_pull_req request{ node.network_params.network };
	request.id = 7;
	request.type = nano::messages::asc_pull_type::blocks;

	nano::messages::asc_pull_req::blocks_payload request_payload{};
	request_payload.start = nano::test::random_account ();
	request_payload.count = nano::bootstrap_server::max_blocks;
	request_payload.start_type = nano::messages::asc_pull_req::hash_type::account;
	request.payload = request_payload;
	request.update_header ();

	node.inbound (request, nano::test::fake_channel (node));

	ASSERT_TIMELY_EQ (5s, responses.size (), 1);

	// The response matches the request and carries no blocks
	auto response = responses.get ().front ();
	ASSERT_EQ (response.id, 7);
	ASSERT_EQ (response.type, nano::messages::asc_pull_type::blocks);
	auto response_payload = std::get<nano::messages::asc_pull_ack::blocks_payload> (response.payload);
	ASSERT_TRUE (response_payload.blocks.empty ());
}

/*
 * An account info request may name the account by one of its block hashes instead of the account itself.
 * The hash of a block from the middle of the chain resolves to the chain's account and the response describes that account: its open block, head, block count and confirmed frontier.
 */
TEST (bootstrap_server, serve_account_info_by_hash)
{
	nano::test::system system{};
	auto & node = *system.add_node ();

	responses_helper responses;
	responses.connect (node.bootstrap_server);

	auto chains = nano::test::setup_chains (system, node, /* chain count */ 1, /* block count */ 4);
	auto [account, blocks] = chains.front ();

	// Target a block from the middle of the chain, declared as a block hash
	nano::messages::asc_pull_req request{ node.network_params.network };
	request.id = 7;
	request.type = nano::messages::asc_pull_type::account_info;

	nano::messages::asc_pull_req::account_info_payload request_payload{};
	request_payload.target = blocks[2]->hash ();
	request_payload.target_type = nano::messages::asc_pull_req::hash_type::block;
	request.payload = request_payload;
	request.update_header ();

	node.inbound (request, nano::test::fake_channel (node));

	ASSERT_TIMELY_EQ (5s, responses.size (), 1);

	// The response resolves the hash to its account and describes the whole confirmed chain
	auto response = responses.get ().front ();
	ASSERT_EQ (response.id, 7);
	ASSERT_EQ (response.type, nano::messages::asc_pull_type::account_info);
	auto response_payload = std::get<nano::messages::asc_pull_ack::account_info_payload> (response.payload);
	ASSERT_EQ (response_payload.account, account);
	ASSERT_EQ (response_payload.account_open, blocks.front ()->hash ());
	ASSERT_EQ (response_payload.account_head, blocks.back ()->hash ());
	ASSERT_EQ (response_payload.account_block_count, blocks.size ());
	ASSERT_EQ (response_payload.account_conf_frontier, blocks.back ()->hash ());
	ASSERT_EQ (response_payload.account_conf_height, blocks.size ());
}

/*
 * A disabled bootstrap server drops every request without an answer.
 * The ledger holds the requested account and the request is well formed, so the enable flag is the only reason; the request is neither queued, counted nor answered.
 */
TEST (bootstrap_server, serve_disabled)
{
	nano::test::system system{};
	nano::node_config config;
	config.bootstrap_server->enable = false;
	auto & node = *system.add_node (config);

	responses_helper responses;
	responses.connect (node.bootstrap_server);

	auto chains = nano::test::setup_chains (system, node, /* chain count */ 1, /* block count */ 4);
	auto [account, blocks] = chains.front ();

	// A request an enabled server would answer with the whole chain
	nano::messages::asc_pull_req request{ node.network_params.network };
	request.id = 7;
	request.type = nano::messages::asc_pull_type::blocks;

	nano::messages::asc_pull_req::blocks_payload request_payload{};
	request_payload.start = account;
	request_payload.count = nano::bootstrap_server::max_blocks;
	request_payload.start_type = nano::messages::asc_pull_req::hash_type::account;
	request.payload = request_payload;
	request.update_header ();

	node.inbound (request, nano::test::fake_channel (node));

	// Nothing is answered, and the request was never accepted into the queue nor rejected as invalid
	ASSERT_ALWAYS (1s, responses.size () == 0);
	ASSERT_EQ (node.stats.count (nano::stat::type::bootstrap_server, nano::stat::detail::request), 0);
	ASSERT_EQ (node.stats.count (nano::stat::type::bootstrap_server, nano::stat::detail::invalid), 0);
}

/*
 * A channel may hold only channel_limit queued requests; any further request from it is dropped and counted as overfill.
 * The server runs no worker thread, so nothing drains the queue while three requests arrive over one channel whose limit is two: the first two are queued and the third is dropped.
 */
TEST (bootstrap_server, serve_overfill)
{
	nano::test::system system{};
	nano::node_config config;
	config.bootstrap_server->threads = 0;
	config.bootstrap_server->channel_limit = 2;
	auto & node = *system.add_node (config);

	responses_helper responses;
	responses.connect (node.bootstrap_server);

	auto chains = nano::test::setup_chains (system, node, /* chain count */ 1, /* block count */ 4);
	auto [account, blocks] = chains.front ();

	// Three well formed requests arrive over the same channel, one more than it may queue
	auto channel = nano::test::fake_channel (node);
	for (int n = 0; n < 3; ++n)
	{
		nano::messages::asc_pull_req request{ node.network_params.network };
		request.id = n;
		request.type = nano::messages::asc_pull_type::blocks;

		nano::messages::asc_pull_req::blocks_payload request_payload{};
		request_payload.start = account;
		request_payload.count = nano::bootstrap_server::max_blocks;
		request_payload.start_type = nano::messages::asc_pull_req::hash_type::account;
		request.payload = request_payload;
		request.update_header ();

		node.inbound (request, channel);
	}

	// Two requests are accepted and the third is dropped as overfill, attributed to its request type
	ASSERT_TIMELY_EQ (5s, node.stats.count (nano::stat::type::bootstrap_server, nano::stat::detail::overfill), 1);
	ASSERT_TIMELY_EQ (5s, node.stats.count (nano::stat::type::bootstrap_server, nano::stat::detail::request), 2);
	ASSERT_EQ (node.stats.count (nano::stat::type::bootstrap_server_overfill, nano::stat::detail::blocks), 1);
	ASSERT_ALWAYS (1s, responses.size () == 0);
}

/*
 * A blocks random request naming more hashes than a request may carry is dropped before any lookup.
 * Such a request cannot arrive over the wire, where the deserializer stops at the limit, so it is handed to the server directly; it counts as invalid and nothing is sent back.
 */
TEST (bootstrap_server, serve_blocks_random_too_many)
{
	nano::test::system system{};
	auto & node = *system.add_node ();

	responses_helper responses;
	responses.connect (node.bootstrap_server);

	// One hash more than the request limit, handed over without serialization, which asserts on the size
	nano::messages::asc_pull_req request{ node.network_params.network };
	request.id = 7;
	request.type = nano::messages::asc_pull_type::blocks_random;

	nano::messages::asc_pull_req::blocks_random_payload payload{};
	while (payload.hashes.size () <= nano::bootstrap_server::max_random_hashes)
	{
		payload.hashes.push_back (nano::test::random_hash ());
	}
	request.payload = payload;

	// The server refuses it outright
	ASSERT_FALSE (node.bootstrap_server.request (request, nano::test::fake_channel (node)));
	ASSERT_EQ (node.stats.count (nano::stat::type::bootstrap_server, nano::stat::detail::invalid), 1);
	ASSERT_ALWAYS (1s, responses.size () == 0);
}

/*
 * An account info request by a block hash the ledger does not hold is answered with the zero account and zero fields.
 * No account can be resolved from the hash, so unlike a request for an unknown account, which echoes the requested account, the response names no account at all.
 */
TEST (bootstrap_server, serve_account_info_missing_hash)
{
	nano::test::system system{};
	auto & node = *system.add_node ();

	responses_helper responses;
	responses.connect (node.bootstrap_server);

	nano::test::setup_chains (system, node, /* chain count */ 1, /* block count */ 4);

	// Ask about the account owning a hash the ledger has never seen
	nano::messages::asc_pull_req request{ node.network_params.network };
	request.id = 7;
	request.type = nano::messages::asc_pull_type::account_info;

	nano::messages::asc_pull_req::account_info_payload request_payload{};
	request_payload.target = nano::test::random_hash ();
	request_payload.target_type = nano::messages::asc_pull_req::hash_type::block;
	request.payload = request_payload;
	request.update_header ();

	node.inbound (request, nano::test::fake_channel (node));

	ASSERT_TIMELY_EQ (5s, responses.size (), 1);

	// Nothing resolves, so every field including the account is zero
	auto response = responses.get ().front ();
	ASSERT_EQ (response.id, 7);
	ASSERT_EQ (response.type, nano::messages::asc_pull_type::account_info);
	auto response_payload = std::get<nano::messages::asc_pull_ack::account_info_payload> (response.payload);
	ASSERT_EQ (response_payload.account, nano::account{ 0 });
	ASSERT_EQ (response_payload.account_open, 0);
	ASSERT_EQ (response_payload.account_head, 0);
	ASSERT_EQ (response_payload.account_block_count, 0);
	ASSERT_EQ (response_payload.account_conf_frontier, 0);
	ASSERT_EQ (response_payload.account_conf_height, 0);
}

/*
 * A frontiers request starting past the last account is answered with an empty frontiers response rather than dropped.
 * The start is the highest possible account, so the ledger holds nothing at or above it; the count is valid and the response simply carries no frontiers.
 */
TEST (bootstrap_server, serve_frontiers_end)
{
	nano::test::system system{};
	auto & node = *system.add_node ();

	responses_helper responses;
	responses.connect (node.bootstrap_server);

	nano::test::setup_chains (system, node, /* chain count */ 4, /* block count */ 4);

	// Request frontiers from the highest account there can be
	nano::messages::asc_pull_req request{ node.network_params.network };
	request.id = 7;
	request.type = nano::messages::asc_pull_type::frontiers;

	nano::messages::asc_pull_req::frontiers_payload request_payload{};
	request_payload.start = nano::account{ std::numeric_limits<nano::uint256_t>::max () };
	request_payload.count = nano::bootstrap_server::max_frontiers;
	request.payload = request_payload;
	request.update_header ();

	node.inbound (request, nano::test::fake_channel (node));

	ASSERT_TIMELY_EQ (5s, responses.size (), 1);

	// A response of the right type with no frontiers, nothing counted as invalid
	auto response = responses.get ().front ();
	ASSERT_EQ (response.id, 7);
	ASSERT_EQ (response.type, nano::messages::asc_pull_type::frontiers);
	auto response_payload = std::get<nano::messages::asc_pull_ack::frontiers_payload> (response.payload);
	ASSERT_TRUE (response_payload.frontiers.empty ());
	ASSERT_EQ (node.stats.count (nano::stat::type::bootstrap_server, nano::stat::detail::invalid), 0);
}
