#include <gtest/gtest.h>
#include <mu_coin/ledger.hpp>
#include <mu_coin/address.hpp>
#include <mu_coin/block.hpp>
#include <cryptopp/filters.h>

TEST (ledger, empty)
{
    mu_coin::ledger ledger;
    ASSERT_FALSE (ledger.has_balance (mu_coin::address (0)));
}

TEST (ledger, genesis_balance)
{
    mu_coin::EC::PrivateKey prv;
    prv.Initialize (mu_coin::pool (), mu_coin::curve ());
    mu_coin::EC::PublicKey pub;
    prv.MakePublicKey (pub);
    mu_coin::uint256_union point (pub);
    mu_coin::transaction_block genesis;
    boost::multiprecision::uint256_t max (std::numeric_limits <boost::multiprecision::uint256_t>::max ());
    mu_coin::entry genesis_output (0, max);
    genesis.sign (prv);
    mu_coin::ledger ledger;
    ledger.blocks [genesis.hash ()] = &genesis;
    ledger.latest [point.number()] = &genesis;
}