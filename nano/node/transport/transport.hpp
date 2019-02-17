#pragma once

#include <boost/asio/buffer.hpp>

#include <nano/node/stats.hpp>

namespace nano
{
class message;
class message_sink
{
public:
	virtual ~message_sink () = default;
	virtual size_t hash_code () const = 0;
	virtual bool operator== (nano::message_sink const &) const = 0;
	void sink (nano::message const &, std::function<void(boost::system::error_code const &, size_t)> const & = nullptr) const;
	void send_buffer (std::shared_ptr<std::vector<uint8_t>>, nano::stat::detail, std::function<void(boost::system::error_code const &, size_t)> const & = nullptr) const;
	virtual void send_buffer_raw (boost::asio::const_buffer, std::function<void(boost::system::error_code const &, size_t)> const &) const = 0;
	virtual std::function<void(boost::system::error_code const &, size_t)> callback (std::shared_ptr<std::vector<uint8_t>>, nano::stat::detail, std::function<void(boost::system::error_code const &, size_t)> const & = nullptr) const = 0;
	virtual std::string to_string () const = 0;
};
}

namespace std
{
template <>
struct hash<::nano::message_sink>
{
	size_t operator() (::nano::message_sink const & sink_a) const
	{
		return sink_a.hash_code ();
	}
};
template <>
struct equal_to<std::reference_wrapper<::nano::message_sink const>>
{
	bool operator() (std::reference_wrapper<::nano::message_sink const> const & lhs, std::reference_wrapper<::nano::message_sink const> const & rhs) const
	{
		return lhs.get () == rhs.get ();
	}
};
}

namespace boost
{
template <>
struct hash<::nano::message_sink>
{
	size_t operator() (::nano::message_sink const & sink_a) const
	{
		std::hash<::nano::message_sink> hash;
		return hash (sink_a);
	}
};
template <>
struct hash<std::reference_wrapper<::nano::message_sink const>>
{
	size_t operator() (std::reference_wrapper<::nano::message_sink const> const & sink_a) const
	{
		std::hash<::nano::message_sink> hash;
		return hash (sink_a.get ());
	}
};
}
