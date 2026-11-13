#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/keypair.hpp>
#include <nano/messages/asc_pull.hpp>
#include <nano/node/bootstrap/queries.hpp>
#include <nano/node/bootstrap/verify.hpp>
#include <nano/secure/network_params.hpp>

#include <gtest/gtest.h>

using namespace nano::bootstrap;

namespace
{
std::deque<std::pair<nano::account, nano::block_hash>> make_frontiers (std::initializer_list<uint64_t> accounts)
{
	std::deque<std::pair<nano::account, nano::block_hash>> result;
	for (auto account : accounts)
	{
		result.push_back ({ nano::account{ account }, nano::block_hash{ account } });
	}
	return result;
}
}

/*
 * Response verification: frontiers
 */

TEST (bootstrap_verify, frontiers_empty)
{
	frontiers_query query{ .start = nano::account{ 1 }, .count = 1000 };
	nano::messages::asc_pull_ack::frontiers_payload response;
	ASSERT_EQ (verify (response, query), verify_result::nothing_new);
}

TEST (bootstrap_verify, frontiers_ok)
{
	frontiers_query query{ .start = nano::account{ 1 }, .count = 1000 };
	nano::messages::asc_pull_ack::frontiers_payload response;
	response.frontiers = make_frontiers ({ 2, 3, 5 });
	ASSERT_EQ (verify (response, query), verify_result::ok);
}

TEST (bootstrap_verify, frontiers_unordered)
{
	frontiers_query query{ .start = nano::account{ 1 }, .count = 1000 };
	nano::messages::asc_pull_ack::frontiers_payload response;
	response.frontiers = make_frontiers ({ 5, 3 });
	ASSERT_EQ (verify (response, query), verify_result::invalid);
}

TEST (bootstrap_verify, frontiers_duplicate)
{
	frontiers_query query{ .start = nano::account{ 1 }, .count = 1000 };
	nano::messages::asc_pull_ack::frontiers_payload response;
	response.frontiers = make_frontiers ({ 3, 3 });
	ASSERT_EQ (verify (response, query), verify_result::invalid);
}

TEST (bootstrap_verify, frontiers_below_start)
{
	frontiers_query query{ .start = nano::account{ 10 }, .count = 1000 };
	nano::messages::asc_pull_ack::frontiers_payload response;
	response.frontiers = make_frontiers ({ 5, 11 });
	ASSERT_EQ (verify (response, query), verify_result::invalid);
}

TEST (bootstrap_queries, frontiers_index_keys)
{
	frontiers_query query{ .start = nano::account{ 42 }, .count = 1000 };
	auto keys = index_keys (query);

	ASSERT_EQ (keys.account, query.start);
	ASSERT_EQ (keys.hash, nano::block_hash{ 0 });
}

/*
 * Response verification: blocks
 */

namespace
{
// A small chain of state blocks; verification checks hashes and links, not signatures or work
std::deque<std::shared_ptr<nano::block>> make_chain (size_t count)
{
	std::deque<std::shared_ptr<nano::block>> blocks;
	nano::keypair key;
	nano::block_builder builder;
	nano::block_hash previous{ 0 };
	for (size_t n = 0; n < count; ++n)
	{
		auto block = builder.state ()
					 .make_block ()
					 .account (key.pub)
					 .previous (previous)
					 .representative (key.pub)
					 .balance (count - n)
					 .link (0)
					 .sign (key.prv, key.pub)
					 .work (0)
					 .build ();
		previous = block->hash ();
		blocks.push_back (block);
	}
	return blocks;
}
}

TEST (bootstrap_verify, blocks_empty)
{
	blocks_query query{ .account = nano::account{ 1 }, .start = nano::account{ 1 }, .count = 128, .type = query_type::blocks_by_account };
	nano::messages::asc_pull_ack::blocks_payload response;
	ASSERT_EQ (verify (response, query), verify_result::nothing_new);
}

TEST (bootstrap_verify, blocks_chain_ok)
{
	auto chain = make_chain (3);
	blocks_query query{ .account = chain.front ()->account_field ().value (), .start = chain.front ()->hash (), .count = 128, .type = query_type::blocks_by_hash };
	nano::messages::asc_pull_ack::blocks_payload response;
	response.blocks = chain;
	ASSERT_EQ (verify (response, query), verify_result::ok);
}

TEST (bootstrap_verify, blocks_single_echo)
{
	auto chain = make_chain (1);
	blocks_query query{ .account = chain.front ()->account_field ().value (), .start = chain.front ()->hash (), .count = 128, .type = query_type::blocks_by_hash };
	nano::messages::asc_pull_ack::blocks_payload response;
	response.blocks = chain;
	ASSERT_EQ (verify (response, query), verify_result::nothing_new);
}

TEST (bootstrap_verify, blocks_count_overflow)
{
	auto chain = make_chain (5);
	blocks_query query{ .account = chain.front ()->account_field ().value (), .start = chain.front ()->hash (), .count = 3, .type = query_type::blocks_by_hash };
	nano::messages::asc_pull_ack::blocks_payload response;
	response.blocks = chain;
	ASSERT_EQ (verify (response, query), verify_result::invalid);
}

TEST (bootstrap_verify, blocks_start_mismatch)
{
	auto chain = make_chain (3);
	blocks_query query{ .account = chain.front ()->account_field ().value (), .start = nano::block_hash{ 12345 }, .count = 128, .type = query_type::blocks_by_hash };
	nano::messages::asc_pull_ack::blocks_payload response;
	response.blocks = chain;
	ASSERT_EQ (verify (response, query), verify_result::invalid);
}

TEST (bootstrap_verify, blocks_chain_break)
{
	auto chain = make_chain (3);
	auto foreign = make_chain (1);
	chain[1] = foreign.front (); // Break the previous () linkage
	blocks_query query{ .account = chain.front ()->account_field ().value (), .start = chain.front ()->hash (), .count = 128, .type = query_type::blocks_by_hash };
	nano::messages::asc_pull_ack::blocks_payload response;
	response.blocks = chain;
	ASSERT_EQ (verify (response, query), verify_result::invalid);
}

TEST (bootstrap_verify, blocks_by_account_mismatch)
{
	auto chain = make_chain (2);
	blocks_query query{ .account = nano::account{ 999 }, .start = nano::account{ 999 }, .count = 128, .type = query_type::blocks_by_account };
	nano::messages::asc_pull_ack::blocks_payload response;
	response.blocks = chain;
	ASSERT_EQ (verify (response, query), verify_result::invalid);
}

/*
 * Response verification: topo index
 */

namespace
{
// Topo entries from (height, hash) pairs; distinct hashes let several entries share a height
std::deque<nano::topo_key> make_topo (std::initializer_list<std::pair<uint64_t, uint64_t>> keys)
{
	std::deque<nano::topo_key> result;
	for (auto [height, hash] : keys)
	{
		result.push_back (nano::topo_key{ height, nano::block_hash{ hash } });
	}
	return result;
}
}

TEST (bootstrap_verify, topo_empty)
{
	topo_index_query query{ .start = nano::topo_key{}, .count = 1000 };
	nano::messages::asc_pull_ack::topo_index_payload response;
	ASSERT_EQ (verify (response, query), verify_result::nothing_new);
}

TEST (bootstrap_verify, topo_ok)
{
	topo_index_query query{ .start = nano::topo_key{}, .count = 1000 };
	nano::messages::asc_pull_ack::topo_index_payload response;
	response.entries = make_topo ({ { 1, 1 }, { 2, 1 }, { 3, 1 } });
	ASSERT_EQ (verify (response, query), verify_result::ok);
}

TEST (bootstrap_verify, topo_same_height)
{
	// Several blocks may share a topo height (height delta 0), as long as they stay hash-ordered
	topo_index_query query{ .start = nano::topo_key{}, .count = 1000 };
	nano::messages::asc_pull_ack::topo_index_payload response;
	response.entries = make_topo ({ { 5, 1 }, { 5, 2 }, { 6, 1 } });
	ASSERT_EQ (verify (response, query), verify_result::ok);
}

TEST (bootstrap_verify, topo_start_echo)
{
	// The server echoes the requested start as the first entry; front == start must pass
	topo_index_query query{ .start = nano::topo_key{ 5, nano::block_hash{ 1 } }, .count = 1000 };
	nano::messages::asc_pull_ack::topo_index_payload response;
	response.entries = make_topo ({ { 5, 1 }, { 6, 1 } });
	ASSERT_EQ (verify (response, query), verify_result::ok);
}

TEST (bootstrap_verify, topo_count_overflow)
{
	topo_index_query query{ .start = nano::topo_key{}, .count = 2 };
	nano::messages::asc_pull_ack::topo_index_payload response;
	response.entries = make_topo ({ { 1, 1 }, { 2, 1 }, { 3, 1 } });
	ASSERT_EQ (verify (response, query), verify_result::invalid);
}

TEST (bootstrap_verify, topo_below_start)
{
	topo_index_query query{ .start = nano::topo_key{ 10, nano::block_hash{ 0 } }, .count = 1000 };
	nano::messages::asc_pull_ack::topo_index_payload response;
	response.entries = make_topo ({ { 5, 1 }, { 6, 1 } });
	ASSERT_EQ (verify (response, query), verify_result::invalid);
}

TEST (bootstrap_verify, topo_duplicate)
{
	topo_index_query query{ .start = nano::topo_key{}, .count = 1000 };
	nano::messages::asc_pull_ack::topo_index_payload response;
	response.entries = make_topo ({ { 2, 1 }, { 2, 1 } });
	ASSERT_EQ (verify (response, query), verify_result::invalid);
}

TEST (bootstrap_verify, topo_descending)
{
	topo_index_query query{ .start = nano::topo_key{}, .count = 1000 };
	nano::messages::asc_pull_ack::topo_index_payload response;
	response.entries = make_topo ({ { 3, 1 }, { 2, 1 } });
	ASSERT_EQ (verify (response, query), verify_result::invalid);
}

TEST (bootstrap_verify, topo_skipped_height)
{
	// The core invariant: heights are densely packed, so a page may not jump over a height
	topo_index_query query{ .start = nano::topo_key{}, .count = 1000 };
	nano::messages::asc_pull_ack::topo_index_payload response;
	response.entries = make_topo ({ { 1, 1 }, { 3, 1 } });
	ASSERT_EQ (verify (response, query), verify_result::invalid);
}

/*
 * Response verification: random blocks
 */

namespace
{
std::deque<nano::block_hash> hashes_of (std::deque<std::shared_ptr<nano::block>> const & blocks)
{
	std::deque<nano::block_hash> result;
	for (auto const & block : blocks)
	{
		result.push_back (block->hash ());
	}
	return result;
}
}

TEST (bootstrap_verify, blocks_random_empty)
{
	blocks_random_query query{ .hashes = hashes_of (make_chain (3)) };
	nano::messages::asc_pull_ack::blocks_payload response;
	ASSERT_EQ (verify (response, query), verify_result::nothing_new);
}

TEST (bootstrap_verify, blocks_random_ok)
{
	auto blocks = make_chain (3);
	blocks_random_query query{ .hashes = hashes_of (blocks) };
	nano::messages::asc_pull_ack::blocks_payload response;
	response.blocks = blocks;
	ASSERT_EQ (verify (response, query), verify_result::ok);
}

TEST (bootstrap_verify, blocks_random_partial)
{
	// A random fetch may silently omit hashes the peer does not have; a subset is still valid
	auto blocks = make_chain (3);
	blocks_random_query query{ .hashes = hashes_of (blocks) };
	nano::messages::asc_pull_ack::blocks_payload response;
	response.blocks = { blocks.front () };
	ASSERT_EQ (verify (response, query), verify_result::ok);
}

TEST (bootstrap_verify, blocks_random_overflow)
{
	auto blocks = make_chain (3);
	blocks_random_query query{ .hashes = { blocks.front ()->hash () } }; // Requested a single hash
	nano::messages::asc_pull_ack::blocks_payload response;
	response.blocks = blocks; // But three blocks came back
	ASSERT_EQ (verify (response, query), verify_result::invalid);
}

TEST (bootstrap_verify, blocks_random_unrequested)
{
	auto blocks = make_chain (2);
	auto foreign = make_chain (1);
	blocks_random_query query{ .hashes = hashes_of (blocks) };
	nano::messages::asc_pull_ack::blocks_payload response;
	response.blocks = { foreign.front () }; // A block that was never requested
	ASSERT_EQ (verify (response, query), verify_result::invalid);
}

TEST (bootstrap_verify, blocks_random_duplicate)
{
	auto blocks = make_chain (2);
	blocks_random_query query{ .hashes = hashes_of (blocks) };
	nano::messages::asc_pull_ack::blocks_payload response;
	response.blocks = { blocks.front (), blocks.front () }; // Same block returned twice
	ASSERT_EQ (verify (response, query), verify_result::invalid);
}

/*
 * A single block that is not an echo of the requested start is not mistaken for nothing new.
 * One block answers a pull by a hash it does not carry; the echo rule only applies when that one block is the start itself, so this is a start mismatch and invalid.
 */
TEST (bootstrap_verify, blocks_single_mismatch)
{
	auto chain = make_chain (1);
	blocks_query query{ .account = chain.front ()->account_field ().value (), .start = nano::block_hash{ 12345 }, .count = 128, .type = query_type::blocks_by_hash };
	nano::messages::asc_pull_ack::blocks_payload response;
	response.blocks = chain;
	ASSERT_EQ (verify (response, query), verify_result::invalid);
}

/*
 * A pull by account accepts a chain whose first block belongs to that account.
 * The query names the chain's own account; the first block carries it in its account field and the rest links up, so the response is ok.
 */
TEST (bootstrap_verify, blocks_by_account_ok)
{
	auto chain = make_chain (3);
	auto const account = chain.front ()->account_field ().value ();
	blocks_query query{ .account = account, .start = account, .count = 128, .type = query_type::blocks_by_account };
	nano::messages::asc_pull_ack::blocks_payload response;
	response.blocks = chain;
	ASSERT_EQ (verify (response, query), verify_result::ok);
}

/*
 * A frontiers response holding more entries than the query asked for is invalid, as it is for blocks and topo index pages.
 * Three ascending frontiers answer a query for two; the extra entry alone is enough to reject the whole response.
 */
TEST (bootstrap_verify, frontiers_count_overflow)
{
	frontiers_query query{ .start = nano::account{ 1 }, .count = 2 };
	nano::messages::asc_pull_ack::frontiers_payload response;
	response.frontiers = make_frontiers ({ 2, 3, 5 });
	ASSERT_EQ (verify (response, query), verify_result::invalid);
}

/*
 * A single block pull is answered by exactly the requested block.
 * One block carrying the requested hash is ok, unlike in a chain pull, where a lone echo of the start means nothing new.
 */
TEST (bootstrap_verify, single_block_ok)
{
	auto chain = make_chain (1);
	single_block_query query{ .hash = chain.front ()->hash () };
	nano::messages::asc_pull_ack::blocks_payload response;
	response.blocks = chain;
	ASSERT_EQ (verify (response, query), verify_result::ok);
}

/*
 * An empty response to a single block pull is how a peer says that it does not hold the block.
 * It is a valid answer with nothing new, so the block can be asked for elsewhere without blaming the peer.
 */
TEST (bootstrap_verify, single_block_empty)
{
	single_block_query query{ .hash = nano::block_hash{ 12345 } };
	nano::messages::asc_pull_ack::blocks_payload response;
	ASSERT_EQ (verify (response, query), verify_result::nothing_new);
}

/*
 * A block other than the requested one is invalid, however valid the block itself may be.
 * The response carries one well formed block whose hash is not the one asked for.
 */
TEST (bootstrap_verify, single_block_mismatch)
{
	auto chain = make_chain (1);
	single_block_query query{ .hash = nano::block_hash{ 12345 } };
	nano::messages::asc_pull_ack::blocks_payload response;
	response.blocks = chain;
	ASSERT_EQ (verify (response, query), verify_result::invalid);
}

/*
 * More than one block is invalid even when the first one is the requested block.
 * The pull asks for a count of one, so a peer that appends the block's successor did not answer the question it was asked.
 */
TEST (bootstrap_verify, single_block_overflow)
{
	auto chain = make_chain (2);
	single_block_query query{ .hash = chain.front ()->hash () };
	nano::messages::asc_pull_ack::blocks_payload response;
	response.blocks = chain;
	ASSERT_EQ (verify (response, query), verify_result::invalid);
}

/*
 * A single block query goes out as a blocks pull that starts at the block's hash and stops after one block,
 * the request every peer that serves bootstrap already understands.
 */
TEST (bootstrap_queries, single_block_message)
{
	single_block_query query{ .hash = nano::block_hash{ 12345 } };

	// The message is a blocks pull by block hash for a count of one, under the given id
	auto const message = build_message (query, nano::dev::network_params.network, /* id */ 7);
	ASSERT_EQ (message.id, 7);
	ASSERT_EQ (message.type, nano::messages::asc_pull_type::blocks);
	ASSERT_TRUE (std::holds_alternative<nano::messages::asc_pull_req::blocks_payload> (message.payload));
	auto const & payload = std::get<nano::messages::asc_pull_req::blocks_payload> (message.payload);
	ASSERT_EQ (payload.start.as_block_hash (), query.hash);
	ASSERT_EQ (payload.count, 1);
	ASSERT_EQ (payload.start_type, nano::messages::asc_pull_req::hash_type::block);

	// The request is tracked under the block's hash and reported as its own query type
	auto const keys = index_keys (query);
	ASSERT_EQ (keys.account, nano::account{ 0 });
	ASSERT_EQ (keys.hash, query.hash);
	ASSERT_EQ (to_query_type (query), query_type::single_block);
}
