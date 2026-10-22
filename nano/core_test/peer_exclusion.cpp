#include <nano/node/peer_exclusion.hpp>

#include <gtest/gtest.h>

TEST (peer_exclusion, validate)
{
	std::size_t max_size = 10;

	nano::peer_exclusion excluded_peers{ max_size };

	for (auto i = 0; i < max_size + 1; ++i)
	{
		nano::tcp_endpoint endpoint{ boost::asio::ip::make_address_v6 (boost::asio::ip::v4_mapped, boost::asio::ip::address_v4 (i)), 0 };
		ASSERT_FALSE (excluded_peers.check (endpoint));
		ASSERT_EQ (1, excluded_peers.add (endpoint));
		ASSERT_FALSE (excluded_peers.check (endpoint));
	}

	// The oldest entry must have been removed, because we just overfilled the container
	ASSERT_EQ (max_size, excluded_peers.size ());
	nano::tcp_endpoint oldest{ boost::asio::ip::make_address_v6 (boost::asio::ip::v4_mapped, boost::asio::ip::address_v4 (0x0)), 0 };
	ASSERT_EQ (excluded_peers.score (oldest), 0);

	auto to_seconds = [] (std::chrono::steady_clock::time_point const & timepoint) {
		return static_cast<double> (std::chrono::duration_cast<std::chrono::seconds> (timepoint.time_since_epoch ()).count ());
	};

	// However, the rest of the entries should be present
	nano::tcp_endpoint first{ boost::asio::ip::make_address_v6 (boost::asio::ip::v4_mapped, boost::asio::ip::address_v4 (0x1)), 0 };
	ASSERT_NE (excluded_peers.score (first), 0);

	nano::tcp_endpoint second{ boost::asio::ip::make_address_v6 (boost::asio::ip::v4_mapped, boost::asio::ip::address_v4 (0x2)), 0 };
	ASSERT_NE (excluded_peers.score (second), 0);

	// Check exclusion times
	ASSERT_NEAR (to_seconds (std::chrono::steady_clock::now () + excluded_peers.exclude_time_hours), to_seconds (excluded_peers.until (second)), 2);
	ASSERT_EQ (2, excluded_peers.add (second));
	ASSERT_NEAR (to_seconds (std::chrono::steady_clock::now () + excluded_peers.exclude_time_hours), to_seconds (excluded_peers.until (second)), 2);
	ASSERT_EQ (3, excluded_peers.add (second));
	ASSERT_NEAR (to_seconds (std::chrono::steady_clock::now () + excluded_peers.exclude_time_hours * 3 * 2), to_seconds (excluded_peers.until (second)), 2);
	ASSERT_EQ (max_size, excluded_peers.size ());
}
