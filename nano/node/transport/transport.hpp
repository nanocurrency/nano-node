#pragma once

#include <boost/asio/buffer.hpp>

#include <nano/node/stats.hpp>

namespace nano
{
namespace transport
{
	class message;
	nano::endpoint map_endpoint_to_v6 (nano::endpoint const &);
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
