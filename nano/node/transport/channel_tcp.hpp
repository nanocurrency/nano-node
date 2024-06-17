#pragma once

#include <nano/node/transport/channel.hpp>
#include <nano/node/transport/transport.hpp>

namespace nano::transport
{
class tcp_server;
class tcp_channels;
class channel_tcp;

class channel_tcp : public nano::transport::channel, public std::enable_shared_from_this<channel_tcp>
{
	friend class nano::transport::tcp_channels;

public:
	channel_tcp (nano::node &, std::weak_ptr<nano::transport::socket>);
	~channel_tcp () override;

	void update_endpoints ();

	// TODO: investigate clang-tidy warning about default parameters on virtual/override functions//
	void send_buffer (nano::shared_const_buffer const &, std::function<void (boost::system::error_code const &, std::size_t)> const & = nullptr, nano::transport::buffer_drop_policy = nano::transport::buffer_drop_policy::limiter, nano::transport::traffic_type = nano::transport::traffic_type::generic) override;

	std::string to_string () const override;

	nano::endpoint get_endpoint () const override
	{
		return nano::transport::map_tcp_to_endpoint (get_tcp_endpoint ());
	}

	nano::tcp_endpoint get_tcp_endpoint () const override
	{
		nano::lock_guard<nano::mutex> lk (channel_mutex);
		return endpoint;
	}

	nano::endpoint get_local_endpoint () const override
	{
		nano::lock_guard<nano::mutex> lk (channel_mutex);
		return local_endpoint;
	}

	nano::transport::transport_type get_type () const override
	{
		return nano::transport::transport_type::tcp;
	}

	bool max (nano::transport::traffic_type traffic_type) override
	{
		bool result = true;
		if (auto socket_l = socket.lock ())
		{
			result = socket_l->max (traffic_type);
		}
		return result;
	}

	bool alive () const override
	{
		if (auto socket_l = socket.lock ())
		{
			return socket_l->alive ();
		}
		return false;
	}

	void close () override
	{
		if (auto socket_l = socket.lock ())
		{
			socket_l->close ();
		}
	}

public:
	std::weak_ptr<nano::transport::socket> socket;

private:
	nano::endpoint endpoint;
	nano::endpoint local_endpoint;

public: // Logging
	void operator() (nano::object_stream &) const override;
};
}