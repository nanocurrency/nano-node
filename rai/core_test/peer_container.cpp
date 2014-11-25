#include <gtest/gtest.h>
#include <rai/core/core.hpp>

TEST (peer_container, empty_peers)
{
    rai::peer_container peers (rai::endpoint {});
    auto list (peers.purge_list (std::chrono::system_clock::now ()));
    ASSERT_EQ (0, list.size ());
}

TEST (peer_container, no_recontact)
{
    rai::peer_container peers (rai::endpoint {});
    rai::endpoint endpoint1 (boost::asio::ip::address_v6::loopback (), 10000);
    ASSERT_EQ (0, peers.size ());
    ASSERT_FALSE (peers.insert_peer (endpoint1));
    ASSERT_EQ (1, peers.size ());
    ASSERT_TRUE (peers.insert_peer (endpoint1));
}

TEST (peer_container, no_self_incoming)
{
    rai::endpoint self (boost::asio::ip::address_v6::loopback (), 10000);
    rai::peer_container peers (self);
    peers.insert_peer (self);
    ASSERT_TRUE (peers.peers.empty ());
}

TEST (peer_container, no_self_contacting)
{
    rai::endpoint self (boost::asio::ip::address_v6::loopback (), 10000);
    rai::peer_container peers (self);
    peers.insert_peer (self);
    ASSERT_TRUE (peers.peers.empty ());
}

TEST (peer_container, old_known)
{
    rai::endpoint self (boost::asio::ip::address_v6::loopback (), 10000);
    rai::endpoint other (boost::asio::ip::address_v6::loopback (), 10001);
    rai::peer_container peers (self);
    ASSERT_FALSE (peers.insert_peer (other));
    peers.peers.modify (peers.peers.begin (), [] (rai::peer_information & info) {info.last_contact = std::chrono::system_clock::time_point {};});
    ASSERT_FALSE (peers.known_peer (other));
    ASSERT_TRUE (peers.insert_peer (other));
    ASSERT_TRUE (peers.known_peer (other));
}

TEST (peer_container, reserved_peers_no_contact)
{
    rai::peer_container peers (rai::endpoint {});
    ASSERT_TRUE (peers.insert_peer (rai::endpoint (boost::asio::ip::address_v6::v4_mapped (boost::asio::ip::address_v4 (0x00000001)), 10000)));
    ASSERT_TRUE (peers.insert_peer (rai::endpoint (boost::asio::ip::address_v6::v4_mapped (boost::asio::ip::address_v4 (0xc0000201)), 10000)));
    ASSERT_TRUE (peers.insert_peer (rai::endpoint (boost::asio::ip::address_v6::v4_mapped (boost::asio::ip::address_v4 (0xc6336401)), 10000)));
    ASSERT_TRUE (peers.insert_peer (rai::endpoint (boost::asio::ip::address_v6::v4_mapped (boost::asio::ip::address_v4 (0xcb007101)), 10000)));
    ASSERT_TRUE (peers.insert_peer (rai::endpoint (boost::asio::ip::address_v6::v4_mapped (boost::asio::ip::address_v4 (0xe9fc0001)), 10000)));
    ASSERT_TRUE (peers.insert_peer (rai::endpoint (boost::asio::ip::address_v6::v4_mapped (boost::asio::ip::address_v4 (0xf0000001)), 10000)));
    ASSERT_TRUE (peers.insert_peer (rai::endpoint (boost::asio::ip::address_v6::v4_mapped (boost::asio::ip::address_v4 (0xffffffff)), 10000)));
    ASSERT_EQ (0, peers.size ());
}

TEST (peer_container, split)
{
    rai::peer_container peers (rai::endpoint {});
    auto now (std::chrono::system_clock::now ());
    rai::endpoint endpoint1 (boost::asio::ip::address_v6::any (), 100);
    rai::endpoint endpoint2 (boost::asio::ip::address_v6::any (), 101);
    peers.peers.insert ({endpoint1, now - std::chrono::seconds (1), now - std::chrono::seconds (1)});
    peers.peers.insert ({endpoint2, now + std::chrono::seconds (1), now + std::chrono::seconds (1)});
    auto list (peers.purge_list (now));
    ASSERT_EQ (1, list.size ());
    ASSERT_EQ (endpoint2, list [0].endpoint);
}

TEST (peer_container, fill_random_clear)
{
    rai::peer_container peers (rai::endpoint {});
    std::array <rai::endpoint, 8> target;
    std::fill (target.begin (), target.end (), rai::endpoint (boost::asio::ip::address_v6::loopback (), 10000));
    peers.random_fill (target);
    ASSERT_TRUE (std::all_of (target.begin (), target.end (), [] (rai::endpoint const & endpoint_a) {return endpoint_a == rai::endpoint (boost::asio::ip::address_v6::any (), 0); }));
}

TEST (peer_container, fill_random_full)
{
    rai::peer_container peers (rai::endpoint {});
    for (auto i (0); i < 100; ++i)
    {
        peers.insert_peer (rai::endpoint (boost::asio::ip::address_v6::loopback (), i));
    }
    std::array <rai::endpoint, 8> target;
    std::fill (target.begin (), target.end (), rai::endpoint (boost::asio::ip::address_v6::loopback (), 10000));
    peers.random_fill (target);
    ASSERT_TRUE (std::none_of (target.begin (), target.end (), [] (rai::endpoint const & endpoint_a) {return endpoint_a == rai::endpoint (boost::asio::ip::address_v6::loopback (), 10000); }));
}

TEST (peer_container, fill_random_part)
{
    rai::peer_container peers (rai::endpoint {});
    std::array <rai::endpoint, 8> target;
    auto half (target.size () / 2);
    for (auto i (0); i < half; ++i)
    {
        peers.insert_peer (rai::endpoint (boost::asio::ip::address_v6::loopback (), i + 1));
    }
    std::fill (target.begin (), target.end (), rai::endpoint (boost::asio::ip::address_v6::loopback (), 10000));
    peers.random_fill (target);
    ASSERT_TRUE (std::none_of (target.begin (), target.begin () + half, [] (rai::endpoint const & endpoint_a) {return endpoint_a == rai::endpoint (boost::asio::ip::address_v6::loopback (), 10000); }));
    ASSERT_TRUE (std::none_of (target.begin (), target.begin () + half, [] (rai::endpoint const & endpoint_a) {return endpoint_a == rai::endpoint (boost::asio::ip::address_v6::loopback (), 0); }));
    ASSERT_TRUE (std::all_of (target.begin () + half, target.end (), [] (rai::endpoint const & endpoint_a) {return endpoint_a == rai::endpoint (boost::asio::ip::address_v6::any (), 0); }));
}