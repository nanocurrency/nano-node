#pragma once

#include <nano/node/transport/channel.hpp>
#include <nano/node/transport/transport.hpp>

namespace nano::transport
{
class tcp_server;
class tcp_channels;
class tcp_channel;

class tcp_channel : public nano::transport::channel, public std::enable_shared_from_this<tcp_channel>
{
	friend class nano::transport::tcp_channels;

public:
	tcp_channel (nano::node &, std::weak_ptr<nano::transport::tcp_socket>);
	~tcp_channel () override;

	void update_endpoints ();

	// TODO: investigate clang-tidy warning about default parameters on virtual/override functions//
	void send_buffer (nano::shared_const_buffer const &, std::function<void (boost::system::error_code const &, std::size_t)> const & = nullptr, nano::transport::buffer_drop_policy = nano::transport::buffer_drop_policy::limiter, nano::transport::traffic_type = nano::transport::traffic_type::generic) override;

	std::string to_string () const override;

	nano::endpoint get_remote_endpoint () const override
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		return remote_endpoint;
	}

	nano::endpoint get_local_endpoint () const override
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
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
	std::weak_ptr<nano::transport::tcp_socket> socket;

private:
	nano::endpoint remote_endpoint;
	nano::endpoint local_endpoint;

public: // Logging
	void operator() (nano::object_stream &) const override;
};
}