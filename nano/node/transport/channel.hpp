#pragma once

#include <nano/lib/asio.hpp>
#include <nano/lib/enum_util.hpp>
#include <nano/lib/fwd.hpp>
#include <nano/lib/locks.hpp>
#include <nano/lib/node_capabilities.hpp>
#include <nano/lib/stats.hpp>
#include <nano/messages/fwd.hpp>
#include <nano/messages/keepalive.hpp>
#include <nano/node/bandwidth_limiter.hpp>
#include <nano/node/endpoint.hpp>
#include <nano/node/transport/tcp_socket.hpp>
#include <nano/node/transport/traffic_type.hpp>

#include <boost/asio/ip/network_v6.hpp>

#include <atomic>

namespace nano::transport
{
enum class transport_type : uint8_t
{
	undefined = 0,
	tcp = 1,
	loopback = 2,
	fake = 3
};

struct peer_info final
{
	nano::account node_id{};
	uint8_t protocol_version{ 0 };
	nano::node_capabilities_flags capabilities{};
};

class channel
{
public:
	using callback_t = std::function<void (boost::system::error_code const &, std::size_t)>;

public:
	explicit channel (nano::node &);
	channel (nano::node &, nano::transport::peer_info const &);
	virtual ~channel () = default;

	/// @returns true if the message was sent (or queued to be sent), false if it was immediately dropped
	bool send (nano::messages::message const &, nano::transport::traffic_type, callback_t = nullptr);
	// Same for a message serialized ahead of time, counted as the given type, so one serialization can go to any number of channels
	bool send_buffer (nano::shared_const_buffer const &, nano::messages::message_type, nano::transport::traffic_type, callback_t = nullptr);

	// True when the channel has no room left for the traffic type, so senders should hold that traffic back
	bool max (nano::transport::traffic_type);

	// Closes the connection, after which the channel is no longer alive
	virtual void close () = 0;
	// False once the connection is closed or lost
	virtual bool alive () const;

	// Address of the peer at the other end of the connection
	virtual nano::endpoint get_remote_endpoint () const = 0;
	// This node's own address on the connection
	virtual nano::endpoint get_local_endpoint () const = 0;

	// Address the peer accepts connections on, which other peers get told; the remote endpoint until the peer reports its port
	nano::endpoint get_peering_endpoint () const;
	void set_peering_endpoint (nano::endpoint);

	virtual nano::transport::transport_type get_type () const = 0;
	virtual std::string to_string () const = 0;

	// The peer's identity, protocol version and capabilities
	nano::transport::peer_info const & get_peer_info () const noexcept;
	// Node ID of the peer, zero when the channel has none
	nano::account get_node_id () const;
	// The same node ID, empty instead of zero
	std::optional<nano::account> get_node_id_optional () const;
	// Protocol version the peer runs
	uint8_t get_network_version () const;
	// Capabilities the peer announced
	nano::node_capabilities_flags get_flags () const;
	// Whether the peer announced the capability
	bool has_capability (nano::node_capabilities) const;
	// False when the peer announced it has no ledger to serve
	bool serves_ledger () const;

	// When a message from the peer last arrived
	std::chrono::steady_clock::time_point get_last_packet_received () const;
	void set_last_packet_received (std::chrono::steady_clock::time_point);
	// When a send to the peer last completed, which marks the connection idle once it is too long ago
	std::chrono::steady_clock::time_point get_last_packet_sent () const;
	void set_last_packet_sent (std::chrono::steady_clock::time_point);

	// Latest keepalive from the peer, kept until pop_last_keepalive () takes it so each one is handed out once
	void set_last_keepalive (nano::messages::keepalive const &);
	std::optional<nano::messages::keepalive> pop_last_keepalive ();

	// The node this channel belongs to
	std::shared_ptr<nano::node> owner () const;

public: // Test only
	// Makes max () report the traffic type as full until unsaturate (), so dev network tests can put the channel under congestion
	void saturate (nano::transport::traffic_type);
	void unsaturate (nano::transport::traffic_type);

protected:
	// Serializes the message and hands the buffer over, a channel that delivers message objects overrides it
	virtual bool send_impl (nano::messages::message const &, nano::transport::traffic_type, callback_t);
	virtual bool send_buffer_impl (nano::shared_const_buffer const &, nano::transport::traffic_type, callback_t) = 0;

	// Parses the buffer back into a message object, for a channel that delivers those
	bool send_buffer_as_message (nano::shared_const_buffer const &, nano::transport::traffic_type, callback_t);

	// The channel's own answer to max (), asked only when the traffic type is not saturated
	virtual bool max_impl (nano::transport::traffic_type);

protected:
	nano::node & node;
	mutable nano::mutex mutex;

private:
	nano::transport::peer_info const peer;

	std::optional<nano::endpoint> peering_endpoint{};
	std::chrono::steady_clock::time_point last_packet_received{ std::chrono::steady_clock::now () };
	std::chrono::steady_clock::time_point last_packet_sent{ std::chrono::steady_clock::now () };
	std::optional<nano::messages::keepalive> last_keepalive{};
	nano::enum_array<nano::transport::traffic_type, std::atomic<bool>> saturation{};

public: // Logging
	virtual void operator() (nano::object_stream &) const;
};
}
