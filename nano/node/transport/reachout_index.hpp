#pragma once

#include <nano/lib/fwd.hpp>
#include <nano/node/endpoint.hpp>
#include <nano/node/endpoint_templ.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include <atomic>
#include <chrono>
#include <memory>

namespace mi = boost::multi_index;

using namespace std::chrono_literals;

namespace nano::transport
{
/**
 * One reachout this node started toward an endpoint, and the only way to report how its connection attempt ended.
 * It holds the endpoint back from further reachouts while the attempt runs, and until its expiry after that.
 * The first outcome reported stands, and letting go of the last pointer `reachout_index::track` handed out without one reports a failure
 */
class reachout final
{
	friend class reachout_index;

public:
	// How the connection attempt ended
	enum class outcome
	{
		pending, // Still running
		connected, // Ended in a channel
		failed, // Ended without a channel
		dropped, // Refused by this node, which says nothing about the endpoint
	};

public:
	reachout (nano::tcp_endpoint const &, unsigned failures, std::chrono::steady_clock::time_point start, std::chrono::steady_clock::time_point expiry);

	// The attempt ended in a channel, which clears the failures for the next reachout
	void mark_connected ();
	// This node refused the attempt itself: the endpoint is free at once and the failures stay as they are
	void drop ();

	outcome result () const;

public:
	nano::tcp_endpoint const endpoint; // Endpoint reached out to
	boost::asio::ip::address const address; // Its address normalized with ipv4_address_or_ipv6_subnet, the per IP key
	boost::asio::ip::address const subnetwork; // Its subnetwork, the per subnetwork key
	std::chrono::steady_clock::time_point const start; // When the reachout began
	std::chrono::steady_clock::time_point const expiry; // When the endpoint may be reached out to again
	unsigned const failures; // Reachouts to the endpoint right before this one that ended without a channel, each doubling the hold

private:
	// Records the outcome unless one is recorded already
	void resolve (outcome);

private:
	std::atomic<outcome> state{ outcome::pending }; // Written by whichever thread ends the attempt, read by the index under its owner's lock
};

/**
 * Reachouts to endpoints, one per endpoint, each holding its endpoint back while its attempt runs and for a cooldown that doubles over reachouts ending without a channel.
 * An expired record is remembered for its failures until the longest cooldown has passed once more.
 * Holds no lock and applies no policy, the channels container drives it under its own mutex and tests drive it directly
 */
class reachout_index final
{
public:
	// Bounds the index applies, filled by the channels container from the network config
	struct limits
	{
		std::chrono::seconds cooldown{ 10s }; // Hold after a reachout, 0 disables holding
		std::chrono::seconds cooldown_max{ 5min }; // Longest hold, and how long an expired record is remembered
		size_t max_size{ 16 * 1024 }; // Records kept at most, the ones expiring first are evicted beyond it unless their attempt still runs
	};

public:
	explicit reachout_index (limits const &);

	// Records a reachout to the endpoint and hands out the handle its attempt reports through, null while an earlier one still holds the endpoint
	std::shared_ptr<reachout> track (nano::tcp_endpoint const &, std::chrono::steady_clock::time_point now);
	// Forgets the records remembered for too long and evicts down to the size limit, sparing running attempts, returns how many were evicted
	size_t purge (std::chrono::steady_clock::time_point now);

	// Records still holding an endpoint at the address, or in its subnetwork
	size_t count_per_ip (boost::asio::ip::address const &, std::chrono::steady_clock::time_point now) const;
	size_t count_per_subnetwork (boost::asio::ip::address const &, std::chrono::steady_clock::time_point now) const;

	size_t size () const;

	nano::container_info container_info () const;

private:
	// The hold of a reachout after the given failures
	std::chrono::seconds hold (unsigned failures) const;
	// Whether the record still holds its endpoint
	static bool holding (reachout const &, std::chrono::steady_clock::time_point now);

private:
	limits const config;

	// clang-format off
	class tag_endpoint {};
	class tag_address {};
	class tag_subnetwork {};
	class tag_expiry {};

	using ordered_reachouts = boost::multi_index_container<std::shared_ptr<reachout>,
	mi::indexed_by<
		mi::hashed_unique<mi::tag<tag_endpoint>,
			mi::member<reachout, nano::tcp_endpoint const, &reachout::endpoint>,
			boost::hash<nano::tcp_endpoint>>,
		mi::hashed_non_unique<mi::tag<tag_address>,
			mi::member<reachout, boost::asio::ip::address const, &reachout::address>,
			boost::hash<boost::asio::ip::address>>,
		mi::hashed_non_unique<mi::tag<tag_subnetwork>,
			mi::member<reachout, boost::asio::ip::address const, &reachout::subnetwork>,
			boost::hash<boost::asio::ip::address>>,
		mi::ordered_non_unique<mi::tag<tag_expiry>,
			mi::member<reachout, std::chrono::steady_clock::time_point const, &reachout::expiry>>
	>>;
	// clang-format on

	ordered_reachouts reachouts;
};
}
