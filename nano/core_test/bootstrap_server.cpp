#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <iterator>

using namespace std::chrono_literals;

namespace
{
using block_list_t = std::vector<std::shared_ptr<nano::block>>;

/**
 * Creates `block_count` random send blocks in an account chain
 */
block_list_t setup_chain (nano::test::system & system, nano::node & node, nano::keypair key, int block_count)
{
	auto latest = node.latest (key.pub);
	auto balance = node.balance (key.pub);

	std::vector<std::shared_ptr<nano::block>> blocks;
	for (int n = 0; n < block_count; ++n)
	{
		nano::keypair throwaway;
		nano::block_builder builder;

		balance -= 1;
		auto send = builder
					.send ()
					.previous (latest)
					.destination (throwaway.pub)
					.balance (balance)
					.sign (key.prv, key.pub)
					.work (*system.work.generate (latest))
					.build_shared ();

		latest = send->hash ();
		blocks.push_back (send);
	}

	EXPECT_TRUE (nano::test::process (node, blocks));
	// Confirm whole chain at once
	EXPECT_TRUE (nano::test::confirm (node, { blocks.back () }));
	EXPECT_TIMELY (5s, nano::test::confirmed (node, blocks));

	return blocks;
}

/**
 * Creates `count` account chains, each with `block_count` blocks
 */
std::vector<std::pair<nano::account, block_list_t>> setup_chains (nano::test::system & system, nano::node & node, int count, int block_count)
{
	auto latest = node.latest (nano::dev::genesis_key.pub);
	auto balance = node.balance (nano::dev::genesis_key.pub);

	std::vector<std::pair<nano::account, block_list_t>> chains;
	for (int n = 0; n < count; ++n)
	{
		nano::keypair key;
		nano::block_builder builder;

		balance -= block_count * 2; // Send enough to later create `block_count` blocks
		auto send = builder
					.send ()
					.previous (latest)
					.destination (key.pub)
					.balance (balance)
					.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					.work (*system.work.generate (latest))
					.build_shared ();

		auto open = builder
					.open ()
					.source (send->hash ())
					.representative (key.pub)
					.account (key.pub)
					.sign (key.prv, key.pub)
					.work (*system.work.generate (key.pub))
					.build_shared ();

		latest = send->hash ();

		// Ensure blocks are in the ledger and confirmed
		EXPECT_TRUE (nano::test::process (node, { send, open }));
		EXPECT_TRUE (nano::test::confirm (node, { send, open }));
		EXPECT_TIMELY (5s, nano::test::confirmed (node, { send, open }));

		auto added_blocks = setup_chain (system, node, key, block_count);

		auto blocks = block_list_t{ open };
		blocks.insert (blocks.end (), added_blocks.begin (), added_blocks.end ());

		chains.emplace_back (key.pub, blocks);
	}

	return chains;
}

/**
 * Helper to track responses in thread safe way
 */
class responses_helper final
{
public:
	void add (nano::asc_pull_ack & ack)
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		responses.push_back (ack);
	}

	std::vector<nano::asc_pull_ack> get ()
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		return responses;
	}

	std::size_t size ()
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		return responses.size ();
	}

private:
	nano::mutex mutex;
	std::vector<nano::asc_pull_ack> responses;
};

/**
 * Checks if both lists contain the same blocks, with `blocks_b` skipped by `skip` elements
 */
bool compare_blocks (std::vector<std::shared_ptr<nano::block>> blocks_a, std::vector<std::shared_ptr<nano::block>> blocks_b, int skip = 0)
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
	node.bootstrap_server.on_response.add ([&] (auto & response, auto & channel) {
		responses.add (response);
	});

	auto chains = setup_chains (system, node, 1, 128);
	auto [first_account, first_blocks] = chains.front ();

	// Request blocks from account root
	nano::asc_pull_req request{ node.network_params.network };
	request.id = 7;
	request.type = nano::asc_pull_type::blocks;

	nano::asc_pull_req::blocks_payload request_payload;
	request_payload.start = first_account;
	request_payload.count = nano::bootstrap_server::max_blocks;
	request.payload = request_payload;

	node.network.inbound (request, nano::test::fake_channel (node));

	ASSERT_TIMELY (5s, responses.size () == 1);

	auto response = responses.get ().front ();
	// Ensure we got response exactly for what we asked for
	ASSERT_EQ (response.id, 7);
	ASSERT_EQ (response.type, nano::asc_pull_type::blocks);

	nano::asc_pull_ack::blocks_payload response_payload;
	ASSERT_NO_THROW (response_payload = std::get<nano::asc_pull_ack::blocks_payload> (response.payload));
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
	node.bootstrap_server.on_response.add ([&] (auto & response, auto & channel) {
		responses.add (response);
	});

	auto chains = setup_chains (system, node, 1, 256);
	auto [account, blocks] = chains.front ();

	// Skip a few blocks to request hash in the middle of the chain
	blocks = block_list_t (std::next (blocks.begin (), 9), blocks.end ());

	// Request blocks from the middle of the chain
	nano::asc_pull_req request{ node.network_params.network };
	request.id = 7;
	request.type = nano::asc_pull_type::blocks;

	nano::asc_pull_req::blocks_payload request_payload;
	request_payload.start = blocks.front ()->hash ();
	request_payload.count = nano::bootstrap_server::max_blocks;
	request.payload = request_payload;

	node.network.inbound (request, nano::test::fake_channel (node));

	ASSERT_TIMELY (5s, responses.size () == 1);

	auto response = responses.get ().front ();
	// Ensure we got response exactly for what we asked for
	ASSERT_EQ (response.id, 7);
	ASSERT_EQ (response.type, nano::asc_pull_type::blocks);

	nano::asc_pull_ack::blocks_payload response_payload;
	ASSERT_NO_THROW (response_payload = std::get<nano::asc_pull_ack::blocks_payload> (response.payload));
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
	node.bootstrap_server.on_response.add ([&] (auto & response, auto & channel) {
		responses.add (response);
	});

	auto chains = setup_chains (system, node, 1, 256);
	auto [account, blocks] = chains.front ();

	// Skip a few blocks to request hash in the middle of the chain
	blocks = block_list_t (std::next (blocks.begin (), 9), blocks.end ());

	// Request blocks from the middle of the chain
	nano::asc_pull_req request{ node.network_params.network };
	request.id = 7;
	request.type = nano::asc_pull_type::blocks;

	nano::asc_pull_req::blocks_payload request_payload;
	request_payload.start = blocks.front ()->hash ();
	request_payload.count = 1;
	request.payload = request_payload;

	node.network.inbound (request, nano::test::fake_channel (node));

	ASSERT_TIMELY (5s, responses.size () == 1);

	auto response = responses.get ().front ();
	// Ensure we got response exactly for what we asked for
	ASSERT_EQ (response.id, 7);
	ASSERT_EQ (response.type, nano::asc_pull_type::blocks);

	nano::asc_pull_ack::blocks_payload response_payload;
	ASSERT_NO_THROW (response_payload = std::get<nano::asc_pull_ack::blocks_payload> (response.payload));
	ASSERT_EQ (response_payload.blocks.size (), 1);
	ASSERT_TRUE (response_payload.blocks.front ()->hash () == request_payload.start);
}

TEST (bootstrap_server, serve_end_of_chain)
{
	nano::test::system system{};
	auto & node = *system.add_node ();

	responses_helper responses;
	node.bootstrap_server.on_response.add ([&] (auto & response, auto & channel) {
		responses.add (response);
	});

	auto chains = setup_chains (system, node, 1, 128);
	auto [account, blocks] = chains.front ();

	// Request blocks from account frontier
	nano::asc_pull_req request{ node.network_params.network };
	request.id = 7;
	request.type = nano::asc_pull_type::blocks;

	nano::asc_pull_req::blocks_payload request_payload;
	request_payload.start = blocks.back ()->hash ();
	request_payload.count = nano::bootstrap_server::max_blocks;
	request.payload = request_payload;

	node.network.inbound (request, nano::test::fake_channel (node));

	ASSERT_TIMELY (5s, responses.size () == 1);

	auto response = responses.get ().front ();
	// Ensure we got response exactly for what we asked for
	ASSERT_EQ (response.id, 7);
	ASSERT_EQ (response.type, nano::asc_pull_type::blocks);

	nano::asc_pull_ack::blocks_payload response_payload;
	ASSERT_NO_THROW (response_payload = std::get<nano::asc_pull_ack::blocks_payload> (response.payload));
	// Response should contain only the last block from chain
	ASSERT_EQ (response_payload.blocks.size (), 1);
	ASSERT_EQ (*response_payload.blocks.front (), *blocks.back ());
}

TEST (bootstrap_server, serve_missing)
{
	nano::test::system system{};
	auto & node = *system.add_node ();

	responses_helper responses;
	node.bootstrap_server.on_response.add ([&] (auto & response, auto & channel) {
		responses.add (response);
	});

	auto chains = setup_chains (system, node, 1, 128);

	// Request blocks from account frontier
	nano::asc_pull_req request{ node.network_params.network };
	request.id = 7;
	request.type = nano::asc_pull_type::blocks;

	nano::asc_pull_req::blocks_payload request_payload;
	request_payload.start = nano::test::random_hash ();
	request_payload.count = nano::bootstrap_server::max_blocks;
	request.payload = request_payload;

	node.network.inbound (request, nano::test::fake_channel (node));

	ASSERT_TIMELY (5s, responses.size () == 1);

	auto response = responses.get ().front ();
	// Ensure we got response exactly for what we asked for
	ASSERT_EQ (response.id, 7);
	ASSERT_EQ (response.type, nano::asc_pull_type::blocks);

	nano::asc_pull_ack::blocks_payload response_payload;
	ASSERT_NO_THROW (response_payload = std::get<nano::asc_pull_ack::blocks_payload> (response.payload));
	// There should be nothing sent
	ASSERT_EQ (response_payload.blocks.size (), 0);
}

TEST (bootstrap_server, serve_multiple)
{
	nano::test::system system{};
	auto & node = *system.add_node ();

	responses_helper responses;
	node.bootstrap_server.on_response.add ([&] (auto & response, auto & channel) {
		responses.add (response);
	});

	auto chains = setup_chains (system, node, 32, 16);

	{
		// Request blocks from multiple chains at once
		int next_id = 0;
		for (auto & [account, blocks] : chains)
		{
			// Request blocks from account root
			nano::asc_pull_req request{ node.network_params.network };
			request.id = next_id++;
			request.type = nano::asc_pull_type::blocks;

			nano::asc_pull_req::blocks_payload request_payload;
			request_payload.start = account;
			request_payload.count = nano::bootstrap_server::max_blocks;
			request.payload = request_payload;

			node.network.inbound (request, nano::test::fake_channel (node));
		}
	}

	ASSERT_TIMELY (15s, responses.size () == chains.size ());

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
			ASSERT_EQ (response.type, nano::asc_pull_type::blocks);

			nano::asc_pull_ack::blocks_payload response_payload;
			ASSERT_NO_THROW (response_payload = std::get<nano::asc_pull_ack::blocks_payload> (response.payload));
			ASSERT_EQ (response_payload.blocks.size (), 17); // 1 open block + 16 random blocks
			ASSERT_TRUE (compare_blocks (response_payload.blocks, blocks));

			++next_id;
		}
	}

	// Ensure we don't get any unexpected responses
	ASSERT_ALWAYS (1s, responses.size () == chains.size ());
}