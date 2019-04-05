#pragma once

#include <boost/asio/ssl/stream.hpp>
#include <nano/rpc/rpc_connection.hpp>

namespace nano
{
class rpc_secure;
/**
 * Specialization of nano::rpc_connection for establishing TLS connections.
 * Handshakes with client certificates are supported.
 */
class rpc_connection_secure : public rpc_connection
{
public:
	rpc_connection_secure (nano::node &, nano::rpc_secure &);
	void parse_connection () override;
	void write_completion_handler (std::shared_ptr<nano::rpc_connection> rpc) override;
	/** The TLS handshake callback */
	void handle_handshake (const boost::system::error_code & error);
	/** The TLS async shutdown callback */
	void on_shutdown (const boost::system::error_code & error);

private:
	boost::asio::ssl::stream<boost::asio::ip::tcp::socket &> stream;
};
}
