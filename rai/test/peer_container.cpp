#include <gtest/gtest.h>
#include <rai/core/core.hpp>

TEST (peer_container, no_self_incoming)
{
    rai::endpoint self (boost::asio::ip::address_v4 (0x7f000001), 10000);
    rai::peer_container peers (self);
    ASSERT_FALSE (peers.incoming_from_peer (self));
    ASSERT_TRUE (peers.peers.empty ());
}

TEST (peer_container, no_self_contacting)
{
    rai::endpoint self (boost::asio::ip::address_v4 (0x7f000001), 10000);
    rai::peer_container peers (self);
    peers.contacting_peer (self);
    ASSERT_TRUE (peers.peers.empty ());
}

TEST (peer_container, old_known)
{
    rai::endpoint self (boost::asio::ip::address_v4 (0x7f000001), 10000);
    rai::endpoint other (boost::asio::ip::address_v4 (0x7f000001), 10001);
    rai::peer_container peers (self);
    peers.contacting_peer (other);
    ASSERT_FALSE (peers.known_peer (other));
    peers.incoming_from_peer (other);
    ASSERT_TRUE (peers.known_peer (other));
}

TEST (peer_container, exists)
{
    rai::endpoint self (boost::asio::ip::address_v4 (0x7f000001), 10000);
    rai::endpoint other (boost::asio::ip::address_v4 (0x7f000001), 10001);
    rai::peer_container peers (self);
    ASSERT_TRUE (peers.incoming_from_peer (other));
    ASSERT_TRUE (peers.known_peer (other));
    ASSERT_FALSE (peers.incoming_from_peer (other));
}