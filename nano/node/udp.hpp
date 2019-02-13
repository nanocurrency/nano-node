#pragma once

#include <nano/node/node.hpp>
#include <nano/node/stats.hpp>

namespace nano
{
class message_sink_udp : public nano::message_sink
{
public:
	message_sink_udp (nano::node &, nano::endpoint const &);
	void send_buffer_raw (uint8_t const *, size_t, std::function<void(boost::system::error_code const &, size_t)>) const override;
	std::function<void(boost::system::error_code const &, size_t)> callback (std::shared_ptr<std::vector<uint8_t>>, nano::stat::detail) const override;
	nano::node & node;
	nano::endpoint endpoint;
};
}
