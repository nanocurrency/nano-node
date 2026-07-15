#pragma once

#include <nano/node/transport/channel.hpp>

namespace nano::transport
{
/**
 * A channel that connects to nothing and drops anything sent through it.
 * Used purely as a placeholder / fair-queue partition key where requests must
 * be attributed to a distinct origin but no real peer exists (e.g. bootstrap
 * strategies feeding the block processor).
 */
class null_channel final : public nano::transport::channel
{
public:
	explicit null_channel (nano::node & node) :
		nano::transport::channel{ node }
	{
	}

	void close () override
	{
	}

	nano::endpoint get_remote_endpoint () const override
	{
		return {};
	}

	nano::endpoint get_local_endpoint () const override
	{
		return {};
	}

	std::string to_string () const override
	{
		return "null_channel";
	}

	nano::transport::transport_type get_type () const override
	{
		return nano::transport::transport_type::fake;
	}

protected:
	bool send_impl (nano::messages::message const &, nano::transport::traffic_type, nano::transport::channel::callback_t) override
	{
		return false; // Dropped, this channel is never meant to send
	}
};
}
