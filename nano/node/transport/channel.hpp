#pragma once

#include <celerix/lib/locks.hpp>
#include <celerix/lib/object_stream.hpp>
#include <celerix/lib/stats.hpp>
#include <celerix/node/bandwidth_limiter.hpp>
#include <celerix/node/endpoint.hpp>
#include <celerix/node/messages.hpp>
#include <celerix/node/transport/tcp_socket.hpp>

#include <boost/asio/ip/network_v6.hpp>

namespace celerix::transport
{
enum class transport_type : uint8_t
{
	undefined = 0,
	tcp = 1,
	loopback = 2,
	fake = 3
};

class channel
{
public:
	using callback_t = std::function<void (boost::system::error_code const &, std::size_t)>;

public:
	explicit channel (celerix::node &);
	virtual ~channel () = default;

	/// @returns true if the message was sent (or queued to be sent), false if it was immediately dropped
	bool send (celerix::message const &, celerix::transport::traffic_type, callback_t = nullptr);

	virtual void close () = 0;

	virtual celerix::endpoint get_remote_endpoint () const = 0;
	virtual celerix::endpoint get_local_endpoint () const = 0;

	virtual std::string to_string () const = 0;
	virtual celerix::transport::transport_type get_type () const = 0;

	virtual bool max (celerix::transport::traffic_type)
	{
		return false;
	}

	virtual bool alive () const
	{
		return true;
	}

	std::chrono::steady_clock::time_point get_last_bootstrap_attempt () const
	{
		celerix::lock_guard<celerix::mutex> lock{ mutex };
		return last_bootstrap_attempt;
	}

	void set_last_bootstrap_attempt (std::chrono::steady_clock::time_point const time_a)
	{
		celerix::lock_guard<celerix::mutex> lock{ mutex };
		last_bootstrap_attempt = time_a;
	}

	std::chrono::steady_clock::time_point get_last_packet_received () const
	{
		celerix::lock_guard<celerix::mutex> lock{ mutex };
		return last_packet_received;
	}

	void set_last_packet_received (std::chrono::steady_clock::time_point const time_a)
	{
		celerix::lock_guard<celerix::mutex> lock{ mutex };
		last_packet_received = time_a;
	}

	std::chrono::steady_clock::time_point get_last_packet_sent () const
	{
		celerix::lock_guard<celerix::mutex> lock{ mutex };
		return last_packet_sent;
	}

	void set_last_packet_sent (std::chrono::steady_clock::time_point const time_a)
	{
		celerix::lock_guard<celerix::mutex> lock{ mutex };
		last_packet_sent = time_a;
	}

	std::optional<celerix::account> get_node_id_optional () const
	{
		celerix::lock_guard<celerix::mutex> lock{ mutex };
		return node_id;
	}

	celerix::account get_node_id () const
	{
		celerix::lock_guard<celerix::mutex> lock{ mutex };
		return node_id.value_or (0);
	}

	void set_node_id (celerix::account node_id_a)
	{
		celerix::lock_guard<celerix::mutex> lock{ mutex };
		node_id = node_id_a;
	}

	uint8_t get_network_version () const
	{
		return network_version;
	}

	void set_network_version (uint8_t network_version_a)
	{
		network_version = network_version_a;
	}

	celerix::endpoint get_peering_endpoint () const;
	void set_peering_endpoint (celerix::endpoint endpoint);

	std::shared_ptr<celerix::node> owner () const;

protected:
	virtual bool send_buffer (celerix::shared_const_buffer const &, celerix::transport::traffic_type, callback_t) = 0;

protected:
	celerix::node & node;
	mutable celerix::mutex mutex;

private:
	std::chrono::steady_clock::time_point last_bootstrap_attempt{ std::chrono::steady_clock::time_point () };
	std::chrono::steady_clock::time_point last_packet_received{ std::chrono::steady_clock::now () };
	std::chrono::steady_clock::time_point last_packet_sent{ std::chrono::steady_clock::now () };
	std::optional<celerix::account> node_id{};
	std::atomic<uint8_t> network_version{ 0 };
	std::optional<celerix::endpoint> peering_endpoint{};

public: // Logging
	virtual void operator() (celerix::object_stream &) const;
};
}
