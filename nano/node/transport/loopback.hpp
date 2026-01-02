#pragma once

#include <nano/node/transport/channel.hpp>
#include <nano/node/transport/transport.hpp>

namespace nano::transport
{
class loopback_channel final : public nano::transport::channel, public std::enable_shared_from_this<loopback_channel>
{
public:
	explicit loopback_channel (nano::node & node);

	std::string to_string () const override;

	nano::endpoint get_remote_endpoint () const override
	{
		return endpoint;
	}

	nano::endpoint get_local_endpoint () const override
	{
		return endpoint;
	}

	nano::transport::transport_type get_type () const override
	{
		return nano::transport::transport_type::loopback;
	}

	void close () override
	{
		// Can't be closed
	}

protected:
	bool send_impl (nano::messages::message const &, nano::transport::traffic_type, nano::transport::channel::callback_t) override;

private:
	nano::endpoint const endpoint;
};
}