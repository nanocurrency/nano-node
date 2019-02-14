#pragma once

#include <nano/node/common.hpp>
#include <nano/node/stats.hpp>
#include <nano/node/transport/transport.hpp>

namespace nano
{
class message_sink_udp : public nano::message_sink
{
public:
	message_sink_udp (nano::node &, nano::endpoint const &);
	void send_buffer_raw (uint8_t const *, size_t, std::function<void(boost::system::error_code const &, size_t)>) const override;
	std::function<void(boost::system::error_code const &, size_t)> callback (std::shared_ptr<std::vector<uint8_t>>, nano::stat::detail) const override;
	std::string to_string () const override;
	bool operator== (nano::message_sink_udp const & other_a) const
	{
		return &node == &other_a.node && endpoint == other_a.endpoint;
	}
	nano::node & node;
	nano::endpoint endpoint;
};
}

namespace std
{
template <>
struct hash<::nano::message_sink_udp>
{
	size_t operator() (::nano::message_sink_udp const & sink_a) const
	{
		std::hash<::nano::endpoint> hash;
		return hash (sink_a.endpoint);
	}
};
}
