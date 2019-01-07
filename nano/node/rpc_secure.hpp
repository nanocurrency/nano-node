#pragma once
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <nano/node/rpc.hpp>

namespace nano
{
/**
 * Specialization of nano::rpc with TLS support
 */
class rpc_secure : public rpc
{
public:
	rpc_secure (boost::asio::io_service & service_a, nano::node & node_a, nano::rpc_config const & config_a);

	/** Starts accepting connections */
	void accept () override;

	/** Installs the server certificate, key and DH, and optionally sets up client certificate verification */
	void load_certs (boost::asio::ssl::context & ctx);

	/**
	 * If client certificates are used, this is called to verify them.
	 * @param preverified The TLS preverification status. The callback may revalidate, such as accepting self-signed certs.
	 */
	bool on_verify_certificate (bool preverified, boost::asio::ssl::verify_context & ctx);

	/** The context needs to be shared between sessions to make resumption work */
	boost::asio::ssl::context ssl_context;
};

/**
 * Specialization of nano::rpc_connection for establishing TLS connections.
 * Handshakes with client certificates are supported.
 */
class rpc_connection_secure : public rpc_connection
{
public:
	rpc_connection_secure (nano::node &, nano::rpc_secure &);
	void parse_connection () override;
	void read () override;
	/** The TLS handshake callback */
	void handle_handshake (const boost::system::error_code & error);
	/** The TLS async shutdown callback */
	void on_shutdown (const boost::system::error_code & error);

private:
	boost::asio::ssl::stream<boost::asio::ip::tcp::socket &> stream;
};
}
