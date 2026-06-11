#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/keypair.hpp>
#include <nano/messages/asc_pull.hpp>
#include <nano/node/bootstrap/verify.hpp>

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
