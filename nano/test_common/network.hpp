#pragma once

#include <nano/boost/asio/read.hpp>
#include <nano/boost/asio/write.hpp>
#include <nano/node/common.hpp>

#include <boost/asio/streambuf.hpp>
#include <boost/enable_shared_from_this.hpp>

#include <string>

namespace nano
{
class node;
class system;

namespace transport
{
	class channel;
	class channel_tcp;
}

/** Waits until a TCP connection is established and returns the TCP channel on success*/
std::shared_ptr<nano::transport::channel_tcp> establish_tcp (nano::system &, nano::node &, nano::endpoint const &);

struct simple_socket : public boost::enable_shared_from_this<simple_socket>
{
	std::atomic<bool> connected;
	uint16_t port;
	boost::asio::ip::tcp::endpoint endpoint;
	boost::asio::ip::tcp::socket socket;
	std::string error_message;

	explicit simple_socket (boost::asio::io_context & io_ctx_a, boost::asio::ip::address ip_address_a, uint16_t port_a) :
		connected{ false },
		port{ port_a },
		endpoint{ ip_address_a, port_a },
		socket{ io_ctx_a },
		sent{ 0 },
		received{ 0 }
	{
	}

	std::atomic<int> sent;

	void async_write (std::string message)
	{
		boost::asio::async_write (socket, boost::asio::const_buffer (message.data (), message.size ()),
		boost::asio::transfer_at_least (1),
		[this] (boost::system::error_code const & ec_a, std::size_t bytes_transferred) {
			if (ec_a)
			{
				this->error_message = ec_a.message ();
				std::cerr << this->error_message;
			}
			else
			{
				++sent;
			}
		});
	}

	std::atomic<int> received;

	void async_read ()
	{
		boost::asio::streambuf msg_buffer;
		boost::asio::async_read (socket, msg_buffer, boost::asio::transfer_all (),
		[this] (boost::system::error_code const & ec_a, std::size_t bytes_transferred) {
			if (ec_a)
			{
				this->error_message = ec_a.message ();
				std::cerr << this->error_message;
			}
			else
			{
				++received;
			}
		});
	}
};

struct simple_server_socket final : public simple_socket
{
	boost::asio::ip::tcp::acceptor acceptor;

	explicit simple_server_socket (boost::asio::io_context & io_ctx_a, boost::asio::ip::address ip_address_a, uint16_t port_a) :
		simple_socket{ io_ctx_a, ip_address_a, port_a },
		acceptor{ io_ctx_a }
	{
		accept ();
	}

	void accept ()
	{
		acceptor.open (endpoint.protocol ());
		acceptor.set_option (boost::asio::ip::tcp::acceptor::reuse_address (true));
		acceptor.bind (endpoint);
		acceptor.listen ();
		acceptor.async_accept (socket,
		[this] (boost::system::error_code const & ec_a) {
			if (ec_a)
			{
				this->error_message = ec_a.message ();
				std::cerr << this->error_message;
			}
			else
			{
				this->connected = true;
				this->async_read ();
			}
		});
	}
};

struct simple_client_socket final : public simple_socket
{
	explicit simple_client_socket (boost::asio::io_context & io_ctx_a, boost::asio::ip::address ip_address_a, uint16_t port_a) :
		simple_socket{ io_ctx_a, ip_address_a, port_a }
	{
		socket.async_connect (boost::asio::ip::tcp::endpoint (ip_address_a, port_a),
		[this] (boost::system::error_code const & ec_a) {
			if (ec_a)
			{
				this->error_message = ec_a.message ();
				std::cerr << error_message;
			}
			else
			{
				this->connected = true;
			}
		});
	}
};
}
