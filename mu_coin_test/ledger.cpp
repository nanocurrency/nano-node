#include <gtest/gtest.h>
#include <mu_coin/mu_coin.hpp>
#include <cryptopp/filters.h>

TEST (ledger, empty)
{
    mu_coin::ledger ledger;
    ASSERT_FALSE (ledger.has_balance (mu_coin::address (mu_coin::uint256_union (0))));
}

TEST (ledger, genesis_balance)
{
    mu_coin::EC::PrivateKey prv;
    prv.Initialize (mu_coin::pool (), mu_coin::oid ());
    mu_coin::EC::PublicKey pub;
    prv.MakePublicKey (pub);
    mu_coin::transaction_block genesis;
    boost::multiprecision::uint256_t max (std::numeric_limits <boost::multiprecision::uint256_t>::max ());
    mu_coin::entry entry (pub, max, 0);
    genesis.entries.push_back (entry);
    genesis.entries [0].sign (prv, genesis.hash ());
    mu_coin::ledger ledger;
    ledger.latest [entry.address] = &genesis;
}

TEST (address, two_addresses)
{
    mu_coin::EC::PrivateKey prv1;
    prv1.Initialize (mu_coin::pool (), mu_coin::oid ());
    mu_coin::EC::PublicKey pub1;
    prv1.MakePublicKey (pub1);
    mu_coin::EC::PrivateKey prv2;
    prv2.Initialize (mu_coin::pool (), mu_coin::oid ());
    mu_coin::EC::PublicKey pub2;
    prv2.MakePublicKey (pub2);
    ASSERT_FALSE (pub1 == pub2);
    mu_coin::point_encoding point1 (pub1);
    mu_coin::address addr1 (point1.point ());
    mu_coin::point_encoding point2 (pub2);
    mu_coin::address addr2 (point2.point ());
    ASSERT_FALSE (addr1 == addr2);
}

TEST (ledger, simple_spend)
{
    mu_coin::EC::PrivateKey prv1;
    prv1.Initialize (mu_coin::pool (), mu_coin::oid ());
    mu_coin::EC::PublicKey pub1;
    prv1.MakePublicKey (pub1);
    mu_coin::transaction_block genesis;
    boost::multiprecision::uint256_t max (std::numeric_limits <boost::multiprecision::uint256_t>::max ());
    mu_coin::entry entry1 (pub1, max, 0);
    genesis.entries.push_back (entry1);
    genesis.entries [0].sign (prv1, genesis.hash ());
    mu_coin::ledger ledger;
    ledger.latest [entry1.address] = &genesis;
    mu_coin::EC::PrivateKey prv2;
    prv2.Initialize (mu_coin::pool (), mu_coin::oid ());
    mu_coin::EC::PublicKey pub2;
    prv2.MakePublicKey (pub2);
    mu_coin::transaction_block spend;
    mu_coin::entry entry2 (pub1, 0, 1);
    spend.entries.push_back (entry2);
    mu_coin::entry entry3 (pub2, max - 1, 0);
    spend.entries.push_back (entry3);
    spend.entries [0].sign (prv1, spend.hash ());
    spend.entries [1].sign (prv2, spend.hash ());
    auto error (ledger.process (&spend));
    ASSERT_FALSE (error);
    auto block1 (ledger.latest.find (entry2.address));
    auto block2 (ledger.latest.find (entry3.address));
    ASSERT_EQ (block1->second, block2->second);
}

TEST (ledger, fail_out_of_sequence)
{
    mu_coin::EC::PrivateKey prv1;
    prv1.Initialize (mu_coin::pool (), mu_coin::oid ());
    mu_coin::EC::PublicKey pub1;
    prv1.MakePublicKey (pub1);
    mu_coin::transaction_block genesis;
    boost::multiprecision::uint256_t max (std::numeric_limits <boost::multiprecision::uint256_t>::max ());
    mu_coin::entry entry1 (pub1, max, 0);
    genesis.entries.push_back (entry1);
    genesis.entries [0].sign (prv1, genesis.hash ());
    mu_coin::ledger ledger;
    ledger.latest [entry1.address] = &genesis;
    mu_coin::EC::PrivateKey prv2;
    prv2.Initialize (mu_coin::pool (), mu_coin::oid ());
    mu_coin::EC::PublicKey pub2;
    prv2.MakePublicKey (pub2);
    mu_coin::transaction_block spend;
    mu_coin::entry entry2 (pub1, 0, 2);
    spend.entries.push_back (entry2);
    mu_coin::entry entry3 (pub2, max - 1, 0);
    spend.entries.push_back (entry3);
    spend.entries [0].sign (prv1, spend.hash ());
    spend.entries [1].sign (prv2, spend.hash ());
    auto error (ledger.process (&spend));
    ASSERT_TRUE (error);
}

TEST (ledger, fail_fee_too_high)
{
    mu_coin::EC::PrivateKey prv1;
    prv1.Initialize (mu_coin::pool (), mu_coin::oid ());
    mu_coin::EC::PublicKey pub1;
    prv1.MakePublicKey (pub1);
    mu_coin::transaction_block genesis;
    boost::multiprecision::uint256_t max (std::numeric_limits <boost::multiprecision::uint256_t>::max ());
    mu_coin::entry entry1 (pub1, max, 0);
    genesis.entries.push_back (entry1);
    genesis.entries [0].sign (prv1, genesis.hash ());
    mu_coin::ledger ledger;
    ledger.latest [entry1.address] = &genesis;
    mu_coin::EC::PrivateKey prv2;
    prv2.Initialize (mu_coin::pool (), mu_coin::oid ());
    mu_coin::EC::PublicKey pub2;
    prv2.MakePublicKey (pub2);
    mu_coin::transaction_block spend;
    mu_coin::entry entry2 (pub1, 0, 1);
    spend.entries.push_back (entry2);
    mu_coin::entry entry3 (pub2, max - 2, 0);
    spend.entries.push_back (entry3);
    spend.entries [0].sign (prv1, spend.hash ());
    spend.entries [1].sign (prv2, spend.hash ());
    auto error (ledger.process (&spend));
    ASSERT_TRUE (error);
}

TEST (ledger, fail_fee_too_low)
{
    mu_coin::EC::PrivateKey prv1;
    prv1.Initialize (mu_coin::pool (), mu_coin::oid ());
    mu_coin::EC::PublicKey pub1;
    prv1.MakePublicKey (pub1);
    mu_coin::transaction_block genesis;
    boost::multiprecision::uint256_t max (std::numeric_limits <boost::multiprecision::uint256_t>::max ());
    mu_coin::entry entry1 (pub1, max, 0);
    genesis.entries.push_back (entry1);
    genesis.entries [0].sign (prv1, genesis.hash ());
    mu_coin::ledger ledger;
    ledger.latest [entry1.address] = &genesis;
    mu_coin::EC::PrivateKey prv2;
    prv2.Initialize (mu_coin::pool (), mu_coin::oid ());
    mu_coin::EC::PublicKey pub2;
    prv2.MakePublicKey (pub2);
    mu_coin::transaction_block spend;
    mu_coin::entry entry2 (pub1, 0, 1);
    spend.entries.push_back (entry2);
    mu_coin::entry entry3 (pub2, max - 0, 0);
    spend.entries.push_back (entry3);
    spend.entries [0].sign (prv1, spend.hash ());
    spend.entries [1].sign (prv2, spend.hash ());
    auto error (ledger.process (&spend));
    ASSERT_TRUE (error);
}

TEST (ledger, fail_bad_signature)
{
    mu_coin::EC::PrivateKey prv1;
    prv1.Initialize (mu_coin::pool (), mu_coin::oid ());
    mu_coin::EC::PublicKey pub1;
    prv1.MakePublicKey (pub1);
    mu_coin::transaction_block genesis;
    boost::multiprecision::uint256_t max (std::numeric_limits <boost::multiprecision::uint256_t>::max ());
    mu_coin::entry entry1 (pub1, max, 0);
    genesis.entries.push_back (entry1);
    genesis.entries [0].sign (prv1, genesis.hash ());
    mu_coin::ledger ledger;
    ledger.latest [entry1.address] = &genesis;
    mu_coin::EC::PrivateKey prv2;
    prv2.Initialize (mu_coin::pool (), mu_coin::oid ());
    mu_coin::EC::PublicKey pub2;
    prv2.MakePublicKey (pub2);
    mu_coin::transaction_block spend;
    mu_coin::entry entry2 (pub1, 0, 1);
    spend.entries.push_back (entry2);
    mu_coin::entry entry3 (pub2, max - 1, 0);
    spend.entries.push_back (entry3);
    spend.entries [0].sign (prv1, spend.hash ());
    spend.entries [0].signature.bytes [32] ^= 1;
    spend.entries [1].sign (prv2, spend.hash ());
    auto error (ledger.process (&spend));
    ASSERT_TRUE (error);
}