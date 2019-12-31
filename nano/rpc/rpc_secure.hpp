#pragma once
#include <nano/rpc/rpc.hpp>

#include <boost/asio/ssl/context.hpp>

namespace boost
{
namespace asio
{
	class io_context;
}
}

namespace nano
{
/**
 * Specialization of nano::rpc with TLS support
 */
class rpc_secure : public rpc
{
public:
	rpc_secure (boost::asio::io_context & context_a, nano::rpc_config const & config_a, nano::rpc_handler_interface & rpc_handler_interface_a);

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
}
