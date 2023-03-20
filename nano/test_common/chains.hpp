#pragma once

#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <vector>

/*
 * Helper functions to deal with common chain setup scenarios
 */
namespace nano::test
{
/**
 * Creates `count` random 1 raw send blocks in a `source` account chain
 * @returns created blocks
 */
nano::block_list_t setup_chain (nano::test::system & system, nano::node & node, int count, nano::keypair source = nano::dev::genesis_key, bool confirm = true);

/**
 * Creates `chain_count` account chains, each with `block_count` 1 raw random send blocks, all accounts are seeded from `source` account
 * @returns list of created accounts and their blocks
 */
std::vector<std::pair<nano::account, nano::block_list_t>> setup_chains (nano::test::system & system, nano::node & node, int chain_count, int block_count, nano::keypair source = nano::dev::genesis_key, bool confirm = true);

/**
 * Creates `count` 1 raw send blocks from `source` account, each to randomly created account
 * The `source` account chain is then confirmed, but leaves open blocks unconfirmed
 * @returns list of unconfirmed (open) blocks
 */
nano::block_list_t setup_independent_blocks (nano::test::system & system, nano::node & node, int count, nano::keypair source = nano::dev::genesis_key);

/**
 * Sends `amount` raw from `source` account chain into a newly created account and sets that account as its own representative
 * @return created representative
 */
nano::keypair setup_rep (nano::test::system & system, nano::node & node, nano::uint128_t amount, nano::keypair source = nano::dev::genesis_key);
}
