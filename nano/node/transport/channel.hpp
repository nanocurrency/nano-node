#pragma once

#include <nano/lib/asio.hpp>
#include <nano/lib/fwd.hpp>
#include <nano/lib/locks.hpp>
#include <nano/lib/node_capabilities.hpp>
#include <nano/lib/stats.hpp>
#include <nano/messages/fwd.hpp>
#include <nano/messages/keepalive.hpp>
#include <nano/node/bandwidth_limiter.hpp>
#include <nano/node/endpoint.hpp>
#include <nano/node/transport/tcp_socket.hpp>

#include <boost/asio/ip/network_v6.hpp>

namespace nano::transport
{
// Kind of channel: tcp to a peer, loopback within this process, fake connects to nothing
enum class transport_type : uint8_t
{
	undefined = 0,
	tcp = 1,
	loopback = 2,
	fake = 3
};

/** Identity a peer established in its handshake, fixed for the channel's lifetime */
struct peer_info final
{
	nano::account node_id{}; // Zero until a handshake identified the peer
	uint8_t protocol_version{ 0 }; // Protocol version the peer runs
	nano::node_capabilities_flags capabilities{}; // Capabilities the peer advertised
};

class channel
{
public:
	// Completion callback for a send, with the error and the number of bytes written
	using callback_t = std::function<void (boost::system::error_code const &, std::size_t)>;

public:
	// Channel to an unidentified peer, assumed to run the local protocol version
	explicit channel (nano::node &);
	// Channel to the peer identified in the handshake
	channel (nano::node &, nano::transport::peer_info const &);
	virtual ~channel () = default;

	/// @returns true if the message was sent (or queued to be sent), false if it was immediately dropped
	bool send (nano::messages::message const &, nano::transport::traffic_type, callback_t = nullptr);
	// Same for a message serialized ahead of time, counted as the given type, so one serialization can go to any number of channels
	bool send_buffer (nano::shared_const_buffer const &, nano::messages::message_type, nano::transport::traffic_type, callback_t = nullptr);

	// Close the underlying connection
	virtual void close () = 0;

	// Endpoints of the underlying connection
	virtual nano::endpoint get_remote_endpoint () const = 0;
	virtual nano::endpoint get_local_endpoint () const = 0;

	// Remote endpoint as text, for logs
	virtual std::string to_string () const = 0;
	// Kind of channel
	virtual nano::transport::transport_type get_type () const = 0;

	// Whether the send queue for the traffic type is full, a send would be dropped
	virtual bool max (nano::transport::traffic_type)
	{
		return false;
	}

	// Whether the underlying connection is still open
	virtual bool alive () const
	{
		return true;
	}

	// When the last message arrived from the peer
	std::chrono::steady_clock::time_point get_last_packet_received () const
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		return last_packet_received;
	}
	void set_last_packet_received (std::chrono::steady_clock::time_point const time_a)
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		last_packet_received = time_a;
	}

	// When the last message was sent to the peer, an idle channel gets a keepalive and is eventually closed
	std::chrono::steady_clock::time_point get_last_packet_sent () const
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		return last_packet_sent;
	}
	void set_last_packet_sent (std::chrono::steady_clock::time_point const time_a)
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		last_packet_sent = time_a;
	}

	// Node id from the handshake, empty for an unidentified peer
	std::optional<nano::account> get_node_id_optional () const
	{
		if (peer.node_id.is_zero ())
		{
			return std::nullopt;
		}
		return peer.node_id;
	}
	// Node id from the handshake, zero for an unidentified peer
	nano::account get_node_id () const
	{
		return peer.node_id;
	}

	// Protocol version the peer runs
	uint8_t get_network_version () const
	{
		return peer.protocol_version;
	}

	// All capabilities the peer advertised in its handshake
	nano::node_capabilities_flags get_flags () const
	{
		return peer.capabilities;
	}
	// Whether the peer advertised the capability in its handshake
	bool has_capability (nano::node_capabilities) const;

	// Identity from the handshake: node id, protocol version and capabilities
	nano::transport::peer_info const & get_peer_info () const noexcept
	{
		return peer;
	}

	// Whether the peer maintains a ledger, a peering-only node does not
	bool serves_ledger () const
	{
		return !has_capability (nano::node_capabilities::no_ledger);
	}

	// Endpoint other nodes can reach the peer at, self-reported in its keepalive, otherwise the remote endpoint
	nano::endpoint get_peering_endpoint () const;
	void set_peering_endpoint (nano::endpoint endpoint);

	// Latest keepalive from the peer, held until peer discovery takes it
	void set_last_keepalive (nano::messages::keepalive const & message);
	std::optional<nano::messages::keepalive> pop_last_keepalive ();

	// Node this channel belongs to
	std::shared_ptr<nano::node> owner () const;

protected:
	// Serializes the message and hands the buffer over, a channel that delivers message objects overrides it
	virtual bool send_impl (nano::messages::message const &, nano::transport::traffic_type, callback_t);
	// Hands the serialized message to the transport
	virtual bool send_buffer_impl (nano::shared_const_buffer const &, nano::transport::traffic_type, callback_t) = 0;
	// Parses the buffer back into a message object, for a channel that delivers those
	bool send_buffer_as_message (nano::shared_const_buffer const &, nano::transport::traffic_type, callback_t);

protected:
	nano::node & node; // Owning node
	mutable nano::mutex mutex; // Guards the timestamps, the peering endpoint and the keepalive below

private:
	std::chrono::steady_clock::time_point last_packet_received{ std::chrono::steady_clock::now () }; // When the last message arrived
	std::chrono::steady_clock::time_point last_packet_sent{ std::chrono::steady_clock::now () }; // When the last message was sent, starts at creation so a new channel is not purged as idle
	nano::transport::peer_info const peer; // Identity from the handshake
	std::optional<nano::endpoint> peering_endpoint{}; // Self-reported in the peer's keepalive
	std::optional<nano::messages::keepalive> last_keepalive{}; // Not yet taken by peer discovery

public: // Logging
	virtual void operator() (nano::object_stream &) const;
};
}
