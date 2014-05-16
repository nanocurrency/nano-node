#include <gtest/gtest.h>
#include <mu_coin_network/network.hpp>
#include <boost/thread.hpp>
#include <mu_coin/mu_coin.hpp>

TEST (network, construction)
{
    boost::asio::io_service service;
    mu_coin_network::node node1 (service, 24001);
    node1.receive ();
}

TEST (network, send_keepalive)
{
    boost::asio::io_service service;
    mu_coin_network::node node1 (service, 24001);
    mu_coin_network::node node2 (service, 24002);
    node1.receive ();
    node2.receive ();
    boost::thread network_thread ([&service] () {service.run ();});
    node1.send_keepalive (node2.socket.local_endpoint ());
    boost::this_thread::yield ();
    node1.stop ();
    node2.stop ();
    network_thread.join ();
    ASSERT_EQ (1, node2.keepalive_req_count);
    ASSERT_EQ (1, node1.keepalive_ack_count);
}

TEST (network, send_publish)
{
    boost::asio::io_service service;
    mu_coin_network::node node1 (service, 24001);
    mu_coin_network::node node2 (service, 24002);
    node1.receive ();
    node2.receive ();
    boost::thread network_thread ([&service] () {service.run ();});
    std::unique_ptr <mu_coin::transaction_block> block (new mu_coin::transaction_block);
    mu_coin::entry entry;
    block->entries.push_back (entry);
    node1.send_publish (node2.socket.local_endpoint (), std::move (block));
    boost::this_thread::yield ();
    node1.stop ();
    node2.stop ();
    network_thread.join ();
    ASSERT_EQ (1, node2.publish_req_count);
}