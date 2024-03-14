#pragma once

#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace nano
{
using block_list_t = std::vector<std::shared_ptr<nano::block>>;
}

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
 * \brief Create a pair of send/receive blocks to implement the transfer of "amount" raw from "source" to the unopened account "dest".
 * \param system
 * \param node
 * \param amount the amount of raw to transfer
 * \param source the source account
 * \param dest the destination account
 * \param dest_rep the rep that the dest account should have
 * \param force_confirm force confirm the blocks
 */
std::pair<std::shared_ptr<nano::block>, std::shared_ptr<nano::block>> setup_new_account (nano::test::system & system, nano::node & node, nano::uint128_t const amount, nano::keypair source, nano::keypair dest, nano::account dest_rep, bool force_confirm);

/**
 * Sends `amount` raw from `source` account chain into a newly created account and sets that account as its own representative
 * @return created representative
 */
nano::keypair setup_rep (nano::test::system & system, nano::node & node, nano::uint128_t amount, nano::keypair source = nano::dev::genesis_key);
}
