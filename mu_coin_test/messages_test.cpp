#include <gtest/gtest.h>
#include <messages.pb.h>
#include <mu_coin_network/messages.hpp>
#include <mu_coin/mu_coin.hpp>

TEST (messages, address)
{
    mu_coin::keypair key1;
    mu_coin::address address1 (key1.pub);
    mu_coin_network::address address2;
    address2 << address1;
    mu_coin::address address3;
    auto error1 (address3 << address2);
    ASSERT_FALSE (error1);
    ASSERT_EQ (address1, address3);
}

TEST (messages, block_id)
{
    mu_coin::keypair key1;
    mu_coin::block_id id1 (key1.pub, 7);
    mu_coin_network::block_id id2;
    id2 << id1;
    mu_coin::block_id id3;
    auto error1 (id3 << id2);
    ASSERT_FALSE (error1);
    ASSERT_EQ (id1, id3);
}

TEST (messages, entry)
{
    mu_coin::keypair key1;
    mu_coin::entry entry1 (key1.pub, 11, 7);
    mu_coin_network::entry entry2;
    entry2 << entry1;
    mu_coin::entry entry3;
    auto error1 (entry3 << entry2);
    ASSERT_FALSE (error1);
    ASSERT_EQ (entry1, entry3);
}

TEST (messages, transaction_block)
{
    mu_coin::keypair key1;
    mu_coin::transaction_block block1;
    mu_coin::entry entry1 (key1.pub, 11, 7);
    block1.entries.push_back (entry1);
    mu_coin_network::transaction_block block2;
    block2 << block1;
    mu_coin::transaction_block block3;
    auto error1 (block3 << block2);
    ASSERT_FALSE (error1);
    ASSERT_EQ (block1, block3);
}