#include <nano/node/peer_exclusion.hpp>

#include <gtest/gtest.h>

#include <chrono>

/*
 * The list holds at most its size of peers, dropping the entry due to expire first, and excludes a peer from its second strike on, for longer with every further strike
 */
TEST (peer_exclusion, validate)
{
	std::size_t const max_size = 10;
	nano::peer_exclusion excluded_peers{ max_size };
	auto const endpoint = [] (uint32_t address) {
		return nano::tcp_endpoint{ boost::asio::ip::make_address_v6 (boost::asio::ip::v4_mapped, boost::asio::ip::address_v4 (address)), 0 };
	};
	auto const remaining = [&excluded_peers] (nano::tcp_endpoint const & endpoint) {
		return std::chrono::duration_cast<std::chrono::seconds> (excluded_peers.until (endpoint) - std::chrono::steady_clock::now ()).count ();
	};
	auto const exclusion = std::chrono::seconds (nano::peer_exclusion::exclude_time_hours).count ();

	// A first strike scores one and does not exclude the peer yet
	for (uint32_t i = 0; i <= max_size; ++i)
	{
		ASSERT_FALSE (excluded_peers.check (endpoint (i)));
		ASSERT_EQ (1, excluded_peers.add (endpoint (i)));
		ASSERT_FALSE (excluded_peers.check (endpoint (i)));
	}

	// One peer over the size pushed out the entry due to expire first, here the oldest, and no other
	ASSERT_EQ (max_size, excluded_peers.size ());
	ASSERT_EQ (0, excluded_peers.score (endpoint (0)));
	ASSERT_EQ (1, excluded_peers.score (endpoint (1)));
	ASSERT_EQ (1, excluded_peers.score (endpoint (2)));

	// The second strike reaches the score limit and excludes the peer for the exclusion time
	ASSERT_NEAR (exclusion, remaining (endpoint (2)), 2);
	ASSERT_EQ (2, excluded_peers.add (endpoint (2)));
	ASSERT_TRUE (excluded_peers.check (endpoint (2)));
	ASSERT_NEAR (exclusion, remaining (endpoint (2)), 2);

	// Every strike past the limit excludes it for twice the score's worth of exclusion times, without adding an entry
	ASSERT_EQ (3, excluded_peers.add (endpoint (2)));
	ASSERT_NEAR (exclusion * 3 * 2, remaining (endpoint (2)), 2);
	ASSERT_EQ (max_size, excluded_peers.size ());
}
