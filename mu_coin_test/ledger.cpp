#include <gtest/gtest.h>
#include <mu_coin/mu_coin.hpp>
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
    mu_coin::transaction_block genesis;
    boost::multiprecision::uint256_t max (std::numeric_limits <boost::multiprecision::uint256_t>::max ());
    mu_coin::entry & entry (genesis.entries [mu_coin::address (pub)]);
    entry = mu_coin::entry (0, max);
    entry.sign (prv, genesis.hash ());
    mu_coin::ledger ledger;
    ledger.blocks [genesis.hash ()] = &genesis;
    ledger.latest [mu_coin::address (pub)] = &genesis;
}

TEST (ledger, simple_spend)
{
    mu_coin::EC::PrivateKey prv1;
    prv1.Initialize (mu_coin::pool (), mu_coin::curve ());
    mu_coin::EC::PublicKey pub1;
    prv1.MakePublicKey (pub1);
    mu_coin::transaction_block genesis;
    boost::multiprecision::uint256_t max (std::numeric_limits <boost::multiprecision::uint256_t>::max ());
    mu_coin::entry & entry (genesis.entries [mu_coin::address (pub1)]);
    entry = mu_coin::entry (0, max);
    entry.sign (prv1, genesis.hash ());
    mu_coin::ledger ledger;
    ledger.blocks [genesis.hash ()] = &genesis;
    ledger.latest [mu_coin::address (pub1)] = &genesis;
    mu_coin::EC::PrivateKey prv2;
    prv2.Initialize (mu_coin::pool (), mu_coin::curve ());
    mu_coin::EC::PublicKey pub2;
    prv2.MakePublicKey (pub2);
    mu_coin::transaction_block spend;
    spend.entries [mu_coin::address (pub2)] = mu_coin::entry (mu_coin::uint256_union (pub2).number (), max - 1);
    spend.entries [mu_coin::address (pub1)] = mu_coin::entry (genesis.hash (), 0);
    spend.entries [mu_coin::address (pub2)].sign (prv2, spend.hash ());
    spend.entries [mu_coin::address (pub1)].sign (prv1, spend.hash ());
    auto error (ledger.process (&spend));
    ASSERT_FALSE (error);
}