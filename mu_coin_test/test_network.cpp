#include <gtest/gtest.h>
#include <mu_coin_network/network.hpp>
#include <boost/thread.hpp>

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
    ASSERT_EQ (1, node2.keepalive_req);
    ASSERT_EQ (1, node1.keepalive_ack);
}