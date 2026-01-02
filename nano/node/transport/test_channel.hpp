#pragma once

#include <nano/lib/observer_set.hpp>
#include <nano/node/transport/channel.hpp>
#include <nano/node/transport/fwd.hpp>

namespace nano::transport
{
class test_channel final : public nano::transport::channel
{
public:
	nano::observer_set<nano::messages::message, nano::transport::traffic_type> observers; // Called for each queued message

public:
	explicit test_channel (nano::node &);

	nano::endpoint get_remote_endpoint () const override
	{
		return {};
	}

	nano::endpoint get_local_endpoint () const override
	{
		return {};
	}

	nano::transport::transport_type get_type () const override
	{
		return nano::transport::transport_type::loopback;
	}

	void close () override
	{
		// Can't be closed
	}

	std::string to_string () const override
	{
		return "test_channel";
	}

protected:
	bool send_impl (nano::messages::message const &, nano::transport::traffic_type, callback_t) override;
};
}