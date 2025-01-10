#pragma once

#include <celerix/test_common/system.hpp>
#include <celerix/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace celerix
{
using block_list_t = std::vector<std::shared_ptr<celerix::block>>;
}

/*
 * Helper functions to deal with common chain setup scenarios
 */
namespace celerix::test
{
/**
 * Creates `count` random 1 raw send blocks in a `source` account chain
 * @returns created blocks
 */
celerix::block_list_t setup_chain (celerix::test::system & system, celerix::node & node, int count, celerix::keypair source = celerix::dev::genesis_key, bool confirm = true);

/**
 * Creates `chain_count` account chains, each with `block_count` 1 raw random send blocks, all accounts are seeded from `source` account
 * @returns list of created accounts and their blocks
 */
std::vector<std::pair<celerix::account, celerix::block_list_t>> setup_chains (celerix::test::system & system, celerix::node & node, int chain_count, int block_count, celerix::keypair source = celerix::dev::genesis_key, bool confirm = true);

/**
 * Creates `count` 1 raw send blocks from `source` account, each to randomly created account
 * The `source` account chain is then confirmed, but leaves open blocks unconfirmed
 * @returns list of unconfirmed (open) blocks
 */
celerix::block_list_t setup_independent_blocks (celerix::test::system & system, celerix::node & node, int count, celerix::keypair source = celerix::dev::genesis_key);

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
std::pair<std::shared_ptr<celerix::block>, std::shared_ptr<celerix::block>> setup_new_account (celerix::test::system & system, celerix::node & node, celerix::uint128_t const amount, celerix::keypair source, celerix::keypair dest, celerix::account dest_rep, bool force_confirm);

/**
 * Sends `amount` raw from `source` account chain into a newly created account and sets that account as its own representative
 * @return created representative
 */
celerix::keypair setup_rep (celerix::test::system & system, celerix::node & node, celerix::uint128_t amount, celerix::keypair source = celerix::dev::genesis_key);
}
