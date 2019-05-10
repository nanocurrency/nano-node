#pragma once

#include <boost/asio/buffer.hpp>

#include <nano/node/common.hpp>
#include <nano/node/stats.hpp>

namespace nano
{
namespace transport
{
	class message;
	nano::endpoint map_endpoint_to_v6 (nano::endpoint const &);
	nano::endpoint map_tcp_to_endpoint (nano::tcp_endpoint const &);
	nano::tcp_endpoint map_endpoint_to_tcp (nano::endpoint const &);
	// Unassigned, reserved, self
	bool reserved_address (nano::endpoint const &, bool = false);
	enum class transport_type : uint8_t
	{
		undefined = 0,
		udp = 1,
		tcp = 2
	};
	class channel
	{
	public:
		virtual ~channel () = default;
		virtual size_t hash_code () const = 0;
		virtual bool operator== (nano::transport::channel const &) const = 0;
		void send (nano::message const &, std::function<void(boost::system::error_code const &, size_t)> const & = nullptr) const;
		void send_buffer (std::shared_ptr<std::vector<uint8_t>>, nano::stat::detail, std::function<void(boost::system::error_code const &, size_t)> const & = nullptr) const;
		virtual void send_buffer_raw (boost::asio::const_buffer, std::function<void(boost::system::error_code const &, size_t)> const &) const = 0;
		virtual std::function<void(boost::system::error_code const &, size_t)> callback (std::shared_ptr<std::vector<uint8_t>>, nano::stat::detail, std::function<void(boost::system::error_code const &, size_t)> const & = nullptr) const = 0;
		virtual std::string to_string () const = 0;
		virtual nano::endpoint get_endpoint () const = 0;
		virtual nano::tcp_endpoint get_tcp_endpoint () const = 0;
		virtual nano::transport::transport_type get_type () const = 0;

		std::chrono::steady_clock::time_point get_last_bootstrap_attempt () const
		{
			std::lock_guard<std::mutex> lk (channel_mutex);
			return last_bootstrap_attempt;
		}

		void set_last_bootstrap_attempt (std::chrono::steady_clock::time_point const time_a)
		{
			std::lock_guard<std::mutex> lk (channel_mutex);
			last_bootstrap_attempt = time_a;
		}

		std::chrono::steady_clock::time_point get_last_packet_received () const
		{
			std::lock_guard<std::mutex> lk (channel_mutex);
			return last_packet_received;
		}

		void set_last_packet_received (std::chrono::steady_clock::time_point const time_a)
		{
			std::lock_guard<std::mutex> lk (channel_mutex);
			last_packet_received = time_a;
		}

		std::chrono::steady_clock::time_point get_last_packet_sent () const
		{
			std::lock_guard<std::mutex> lk (channel_mutex);
			return last_packet_sent;
		}

		void set_last_packet_sent (std::chrono::steady_clock::time_point const time_a)
		{
			std::lock_guard<std::mutex> lk (channel_mutex);
			last_packet_sent = time_a;
		}

		boost::optional<nano::account> get_node_id () const
		{
			std::lock_guard<std::mutex> lk (channel_mutex);
			return node_id;
		}

		void set_node_id (nano::account node_id_a)
		{
			std::lock_guard<std::mutex> lk (channel_mutex);
			node_id = node_id_a;
		}

		unsigned get_network_version () const
		{
			return network_version;
		}

		void set_network_version (unsigned network_version_a)
		{
			network_version = network_version_a;
		}

		mutable std::mutex channel_mutex;

	private:
		std::chrono::steady_clock::time_point last_bootstrap_attempt{ std::chrono::steady_clock::time_point () };
		std::chrono::steady_clock::time_point last_packet_received{ std::chrono::steady_clock::time_point () };
		std::chrono::steady_clock::time_point last_packet_sent{ std::chrono::steady_clock::time_point () };
		boost::optional<nano::account> node_id{ boost::none };
		std::atomic<unsigned> network_version{ nano::protocol_version };
	};
} // namespace transport
} // namespace nano

namespace std
{
template <>
struct hash<::nano::transport::channel>
{
	size_t operator() (::nano::transport::channel const & channel_a) const
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
	size_t operator() (::nano::transport::channel const & channel_a) const
	{
		std::hash<::nano::transport::channel> hash;
		return hash (channel_a);
	}
};
template <>
struct hash<std::reference_wrapper<::nano::transport::channel const>>
{
	size_t operator() (std::reference_wrapper<::nano::transport::channel const> const & channel_a) const
	{
		std::hash<::nano::transport::channel> hash;
		return hash (channel_a.get ());
	}
};
}
