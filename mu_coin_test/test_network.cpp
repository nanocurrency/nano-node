#include <gtest/gtest.h>
#include <mu_coin_network/network.hpp>
#include <boost/thread.hpp>
#include <mu_coin/mu_coin.hpp>

TEST (network, construction)
{
    boost::asio::io_service service;
    mu_coin::block_store_memory store;
    mu_coin::ledger ledger (store);
    mu_coin_network::node node1 (service, 24001, ledger);
    node1.receive ();
}

TEST (network, send_keepalive)
{
    boost::asio::io_service service;
    mu_coin::block_store_memory store1;
    mu_coin::ledger ledger1 (store1);
    mu_coin_network::node node1 (service, 24001, ledger1);
    mu_coin::block_store_memory store2;
    mu_coin::ledger ledger2 (store2);
    mu_coin_network::node node2 (service, 24002, ledger2);
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

TEST (network, send_publish)
{
    boost::asio::io_service service;
    mu_coin::block_store_memory store1;
    mu_coin::ledger ledger1 (store1);
    mu_coin_network::node node1 (service, 24001, ledger1);
    mu_coin::block_store_memory store2;
    mu_coin::ledger ledger2 (store2);
    mu_coin_network::node node2 (service, 24002, ledger2);
    node1.receive ();
    node2.receive ();
    std::unique_ptr <mu_coin::transaction_block> block (new mu_coin::transaction_block);
    mu_coin::entry entry;
    block->entries.push_back (entry);
    node1.send_publish (node2.socket.local_endpoint (), std::move (block));
    while (node2.publish_nak_count == 0)
    {
        service.run_one ();
    }
    ASSERT_EQ (1, node2.publish_req_count);
}