#include <gtest/gtest.h>
#include <boost/thread.hpp>
#include <mu_coin/mu_coin.hpp>

TEST (network, construction)
{
    boost::asio::io_service service;
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    mu_coin::node node1 (service, 24001, ledger);
    node1.receive ();
}

TEST (network, send_keepalive)
{
    boost::asio::io_service service;
    mu_coin::block_store store1 (mu_coin::block_store_temp);
    mu_coin::ledger ledger1 (store1);
    mu_coin::node node1 (service, 24001, ledger1);
    mu_coin::block_store store2 (mu_coin::block_store_temp);
    mu_coin::ledger ledger2 (store2);
    mu_coin::node node2 (service, 24002, ledger2);
    node1.receive ();
    node2.receive ();
    node1.send_keepalive (node2.socket.local_endpoint ());
    while (node1.keepalive_ack_count == 0)
    {
        service.run_one ();
    }
    ASSERT_EQ (1, node2.keepalive_req_count);
    ASSERT_EQ (1, node1.keepalive_ack_count);
}

TEST (network, publish_req)
{
    auto block (std::unique_ptr <mu_coin::send_block> (new mu_coin::send_block));
    mu_coin::keypair key1;
    mu_coin::keypair key2;
    block->inputs.push_back (mu_coin::send_input (key1.pub, 0, 200));
    block->signatures.push_back (mu_coin::uint512_union ());
    block->outputs.push_back (mu_coin::send_output (key2.pub, 400));
    mu_coin::publish_req req (std::move (block));
    mu_coin::byte_write_stream stream;
    req.serialize (stream);
    mu_coin::publish_req req2;
    mu_coin::byte_read_stream stream2 (stream.data, stream.size);
    auto error (req2.deserialize (stream2));
    ASSERT_FALSE (error);
    ASSERT_EQ (*req.block, *req2.block);
}

TEST (network, send_discarded_publish)
{
    boost::asio::io_service service;
    mu_coin::block_store store1 (mu_coin::block_store_temp);
    mu_coin::ledger ledger1 (store1);
    mu_coin::node node1 (service, 24001, ledger1);
    mu_coin::block_store store2 (mu_coin::block_store_temp);
    mu_coin::ledger ledger2 (store2);
    mu_coin::node node2 (service, 24002, ledger2);
    node1.receive ();
    node2.receive ();
    std::unique_ptr <mu_coin::send_block> block (new mu_coin::send_block);
    block->inputs.push_back (mu_coin::send_input ());
    node1.publish_block (node2.socket.local_endpoint (), std::move (block));
    while (node2.publish_req_count == 0)
    {
        service.run_one ();
    }
    ASSERT_EQ (1, node2.publish_req_count);
    ASSERT_EQ (0, node1.publish_nak_count);
}

TEST (network, send_invalid_publish)
{
    boost::asio::io_service service;
    mu_coin::block_store store1 (mu_coin::block_store_temp);
    mu_coin::ledger ledger1 (store1);
    mu_coin::node node1 (service, 24001, ledger1);
    mu_coin::block_store store2 (mu_coin::block_store_temp);
    mu_coin::ledger ledger2 (store2);
    mu_coin::node node2 (service, 24002, ledger2);
    node1.receive ();
    node2.receive ();
    std::unique_ptr <mu_coin::send_block> block (new mu_coin::send_block);
    mu_coin::keypair key1;
    block->inputs.push_back (mu_coin::send_input (key1.pub, 0, 20));
    block->signatures.push_back (mu_coin::uint512_union ());
    mu_coin::sign_message (key1.prv, key1.pub, block->hash (), block->signatures.back ());
    node1.publish_block (node2.socket.local_endpoint (), std::move (block));
    while (node1.publish_nak_count == 0)
    {
        service.run_one ();
    }
    ASSERT_EQ (1, node2.publish_req_count);
    ASSERT_EQ (1, node1.publish_nak_count);
}

TEST (network, send_valid_publish)
{
    boost::asio::io_service service;
    mu_coin::keypair key1;
    mu_coin::block_store store1 (mu_coin::block_store_temp);
    store1.genesis_put (key1.pub, 100);
    mu_coin::ledger ledger1 (store1);
    mu_coin::node node1 (service, 24001, ledger1);
    mu_coin::block_store store2 (mu_coin::block_store_temp);
    store2.genesis_put (key1.pub, 100);
    mu_coin::ledger ledger2 (store2);
    mu_coin::node node2 (service, 24002, ledger2);
    node1.receive ();
    node2.receive ();
    mu_coin::keypair key2;
    mu_coin::send_block block2;
    mu_coin::block_hash hash1;
    ASSERT_FALSE (store1.latest_get (key1.pub, hash1));
    block2.inputs.push_back (mu_coin::send_input (key1.pub, hash1, 49));
    block2.signatures.push_back (mu_coin::uint512_union ());
    block2.outputs.push_back (mu_coin::send_output (key2.pub, 50));
    auto hash2 (block2.hash ());
    mu_coin::sign_message (key1.prv, key1.pub, hash2, block2.signatures.back ());
    mu_coin::block_hash hash3;
    ASSERT_FALSE (store2.latest_get (key1.pub, hash3));
    node1.publish_block (node2.socket.local_endpoint (), std::unique_ptr <mu_coin::block> (new mu_coin::send_block (block2)));
    while (node1.publish_ack_count == 0)
    {
        service.run_one ();
    }
    ASSERT_EQ (1, node2.publish_req_count);
    ASSERT_EQ (1, node1.publish_ack_count);
    mu_coin::block_hash hash4;
    ASSERT_FALSE (store2.latest_get (key1.pub, hash4));
    ASSERT_FALSE (hash3 == hash4);
    ASSERT_EQ (hash2, hash4);
}