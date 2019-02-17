#pragma once

#include <nano/node/common.hpp>
#include <nano/node/stats.hpp>
#include <nano/node/transport/transport.hpp>

namespace nano
{
class message_sink_tcp : public nano::message_sink
{
public:
	message_sink_tcp (nano::node &, std::shared_ptr<nano::socket>);
	size_t hash_code () const override;
	bool operator== (nano::message_sink const &) const override;
	void send_buffer_raw (boost::asio::const_buffer, std::function<void(boost::system::error_code const &, size_t)> const &) const override;
	std::function<void(boost::system::error_code const &, size_t)> callback (std::shared_ptr<std::vector<uint8_t>>, nano::stat::detail, std::function<void(boost::system::error_code const &, size_t)> const & = nullptr) const override;
	std::string to_string () const override;
	bool operator== (nano::message_sink_tcp const & other_a) const
	{
		return &node == &other_a.node && socket == other_a.socket;
	}
	nano::node & node;
	std::shared_ptr<nano::socket> socket;
};
}
