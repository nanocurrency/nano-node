#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/rate_limiting.hpp>
#include <nano/lib/stats.hpp>
#include <nano/node/common.hpp>
#include <nano/node/socket.hpp>

#include <boost/asio/ip/network_v6.hpp>

namespace nano
{
class bandwidth_limiter final
{
public:
	// initialize with limit 0 = unbounded
	bandwidth_limiter (double const, std::size_t const);
	bool should_drop (std::size_t const &);
	void reset (double const, std::size_t const);

private:
	nano::rate::token_bucket bucket;
};

namespace transport
{
	class message;
	nano::endpoint map_endpoint_to_v6 (nano::endpoint const &);
	nano::endpoint map_tcp_to_endpoint (nano::tcp_endpoint const &);
	nano::tcp_endpoint map_endpoint_to_tcp (nano::endpoint const &);
	boost::asio::ip::address map_address_to_subnetwork (boost::asio::ip::address const &);
	boost::asio::ip::address ipv4_address_or_ipv6_subnet (boost::asio::ip::address const &);
	// Unassigned, reserved, self
	bool reserved_address (nano::endpoint const &, bool = false);
	static std::chrono::seconds constexpr syn_cookie_cutoff = std::chrono::seconds (5);
	enum class transport_type : uint8_t
	{
		undefined = 0,
		udp = 1,
		tcp = 2,
		loopback = 3
	};
	class channel
	{
	public:
		channel (nano::node &);
		virtual ~channel () = default;
		virtual std::size_t hash_code () const = 0;
		virtual bool operator== (nano::transport::channel const &) const = 0;
		void send (nano::message & message_a, std::function<void (boost::system::error_code const &, std::size_t)> const & callback_a = nullptr, nano::buffer_drop_policy policy_a = nano::buffer_drop_policy::limiter);
		virtual void send_buffer (nano::shared_const_buffer const &, std::function<void (boost::system::error_code const &, std::size_t)> const & = nullptr, nano::buffer_drop_policy = nano::buffer_drop_policy::limiter) = 0;
		virtual std::string to_string () const = 0;
		virtual nano::endpoint get_endpoint () const = 0;
		virtual nano::tcp_endpoint get_tcp_endpoint () const = 0;
		virtual nano::transport::transport_type get_type () const = 0;

		std::chrono::steady_clock::time_point get_last_bootstrap_attempt () const
		{
			nano::lock_guard<nano::mutex> lk (channel_mutex);
			return last_bootstrap_attempt;
		}

		void set_last_bootstrap_attempt (std::chrono::steady_clock::time_point const time_a)
		{
			nano::lock_guard<nano::mutex> lk (channel_mutex);
			last_bootstrap_attempt = time_a;
		}

		std::chrono::steady_clock::time_point get_last_packet_received () const
		{
			nano::lock_guard<nano::mutex> lk (channel_mutex);
			return last_packet_received;
		}

		void set_last_packet_received (std::chrono::steady_clock::time_point const time_a)
		{
			nano::lock_guard<nano::mutex> lk (channel_mutex);
			last_packet_received = time_a;
		}

		std::chrono::steady_clock::time_point get_last_packet_sent () const
		{
			nano::lock_guard<nano::mutex> lk (channel_mutex);
			return last_packet_sent;
		}

		void set_last_packet_sent (std::chrono::steady_clock::time_point const time_a)
		{
			nano::lock_guard<nano::mutex> lk (channel_mutex);
			last_packet_sent = time_a;
		}

		boost::optional<nano::account> get_node_id_optional () const
		{
			nano::lock_guard<nano::mutex> lk (channel_mutex);
			return node_id;
		}

		nano::account get_node_id () const
		{
			nano::lock_guard<nano::mutex> lk (channel_mutex);
			if (node_id.is_initialized ())
			{
				return node_id.get ();
			}
			else
			{
				return 0;
			}
		}

		void set_node_id (nano::account node_id_a)
		{
			nano::lock_guard<nano::mutex> lk (channel_mutex);
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

		mutable nano::mutex channel_mutex;

	private:
		std::chrono::steady_clock::time_point last_bootstrap_attempt{ std::chrono::steady_clock::time_point () };
		std::chrono::steady_clock::time_point last_packet_received{ std::chrono::steady_clock::now () };
		std::chrono::steady_clock::time_point last_packet_sent{ std::chrono::steady_clock::now () };
		boost::optional<nano::account> node_id{ boost::none };
		std::atomic<uint8_t> network_version{ 0 };

	protected:
		nano::node & node;
	};

	class channel_loopback final : public nano::transport::channel
	{
	public:
		channel_loopback (nano::node &);
		std::size_t hash_code () const override;
		bool operator== (nano::transport::channel const &) const override;
		void send_buffer (nano::shared_const_buffer const &, std::function<void (boost::system::error_code const &, std::size_t)> const & = nullptr, nano::buffer_drop_policy = nano::buffer_drop_policy::limiter) override;
		std::string to_string () const override;
		bool operator== (nano::transport::channel_loopback const & other_a) const
		{
			return endpoint == other_a.get_endpoint ();
		}

		nano::endpoint get_endpoint () const override
		{
			return endpoint;
		}

		nano::tcp_endpoint get_tcp_endpoint () const override
		{
			return nano::transport::map_endpoint_to_tcp (endpoint);
		}

		nano::transport::transport_type get_type () const override
		{
			return nano::transport::transport_type::loopback;
		}

	private:
		nano::endpoint const endpoint;
	};
} // namespace transport
} // namespace nano

namespace std
{
template <>
struct hash<::nano::transport::channel>
{
	std::size_t operator() (::nano::transport::channel const & channel_a) const
	{
		return channel_a.hash_code ();
	}
};
template <>
struct equal_to<std::reference_wrapper<::nano::transport::channel const>>
{
	bool operator() (std::reference_wrapper<::nano::transport::channel const> const & lhs, std::reference_wrapper<::nano::transport::channel const> const & rhs) const
	{
		return lhs.get () == rhs.get ();
	}
};
}

namespace boost
{
template <>
struct hash<::nano::transport::channel>
{
	std::size_t operator() (::nano::transport::channel const & channel_a) const
	{
		std::hash<::nano::transport::channel> hash;
		return hash (channel_a);
	}
};
template <>
struct hash<std::reference_wrapper<::nano::transport::channel const>>
{
	std::size_t operator() (std::reference_wrapper<::nano::transport::channel const> const & channel_a) const
	{
		std::hash<::nano::transport::channel> hash;
		return hash (channel_a.get ());
	}
};
}
