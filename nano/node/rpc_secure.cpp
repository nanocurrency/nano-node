#include <nano/node/node.hpp>
#include <nano/node/rpc_secure.hpp>

bool nano::rpc_secure::on_verify_certificate (bool preverified, boost::asio::ssl::verify_context & ctx)
{
	X509_STORE_CTX * cts = ctx.native_handle ();
	auto error (X509_STORE_CTX_get_error (cts));
	switch (error)
	{
		case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT:
			BOOST_LOG (node.log) << "TLS: Unable to get issuer";
			break;
		case X509_V_ERR_CERT_NOT_YET_VALID:
		case X509_V_ERR_ERROR_IN_CERT_NOT_BEFORE_FIELD:
			BOOST_LOG (node.log) << "TLS: Certificate not yet valid";
			break;
		case X509_V_ERR_CERT_HAS_EXPIRED:
		case X509_V_ERR_ERROR_IN_CERT_NOT_AFTER_FIELD:
			BOOST_LOG (node.log) << "TLS: Certificate expired";
			break;
		case X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN:
			if (config.secure.verbose_logging)
			{
				BOOST_LOG (node.log) << "TLS: self signed certificate in chain";
			}

			// Allow self-signed certificates
			preverified = true;
			break;
		case X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT:
			BOOST_LOG (node.log) << "TLS: Self signed certificate not in the list of trusted certs (forgot to subject-hash certificate filename?)";
			break;
		default:
			break;
	}

	if (config.secure.verbose_logging)
	{
		if (error != 0)
		{
			BOOST_LOG (node.log) << "TLS: Error: " << X509_verify_cert_error_string (error);
			BOOST_LOG (node.log) << "TLS: Error chain depth : " << X509_STORE_CTX_get_error_depth (cts);
		}

		X509 * cert = X509_STORE_CTX_get_current_cert (cts);
		char subject_name[512];
		X509_NAME_oneline (X509_get_subject_name (cert), subject_name, sizeof (subject_name) - 1);
		BOOST_LOG (node.log) << "TLS: Verifying: " << subject_name;
		BOOST_LOG (node.log) << "TLS: Verification: " << preverified;
	}
	else if (!preverified)
	{
		BOOST_LOG (node.log) << "TLS: Pre-verification failed. Turn on verbose logging for more information.";
	}

	return preverified;
}

void nano::rpc_secure::load_certs (boost::asio::ssl::context & context_a)
{
	// This is called if the key is password protected
	context_a.set_password_callback (
	[this](std::size_t,
	boost::asio::ssl::context_base::password_purpose) {
		return config.secure.server_key_passphrase;
	});

	// The following two options disables the session cache and enables stateless session resumption.
	// This is necessary because of the way the RPC server abruptly terminate connections.
	SSL_CTX_set_session_cache_mode (context_a.native_handle (), SSL_SESS_CACHE_OFF);
	SSL_CTX_set_options (context_a.native_handle (), SSL_OP_NO_TICKET);

	context_a.set_options (
	boost::asio::ssl::context::default_workarounds | boost::asio::ssl::context::no_sslv2 | boost::asio::ssl::context::no_sslv3 | boost::asio::ssl::context::single_dh_use);

	context_a.use_certificate_chain_file (config.secure.server_cert_path);
	context_a.use_private_key_file (config.secure.server_key_path, boost::asio::ssl::context::pem);
	context_a.use_tmp_dh_file (config.secure.server_dh_path);

	// Verify client certificates?
	if (config.secure.client_certs_path.size () > 0)
	{
		context_a.set_verify_mode (boost::asio::ssl::verify_fail_if_no_peer_cert | boost::asio::ssl::verify_peer);
		context_a.add_verify_path (config.secure.client_certs_path);
		context_a.set_verify_callback ([this](auto preverified, auto & ctx) {
			return this->on_verify_certificate (preverified, ctx);
		});
	}
}

nano::rpc_secure::rpc_secure (boost::asio::io_service & service_a, nano::node & node_a, nano::rpc_config const & config_a) :
rpc (service_a, node_a, config_a),
ssl_context (boost::asio::ssl::context::tlsv12_server)
{
	load_certs (ssl_context);
}

void nano::rpc_secure::accept ()
{
	auto connection (std::make_shared<nano::rpc_connection_secure> (node, *this));
	acceptor.async_accept (connection->socket, [this, connection](boost::system::error_code const & ec) {
		if (acceptor.is_open ())
		{
			accept ();
		}
		if (!ec)
		{
			connection->parse_connection ();
		}
		else
		{
			BOOST_LOG (this->node.log) << boost::str (boost::format ("Error accepting RPC connections: %1%") % ec);
		}
	});
}

nano::rpc_connection_secure::rpc_connection_secure (nano::node & node_a, nano::rpc_secure & rpc_a) :
nano::rpc_connection (node_a, rpc_a),
stream (socket, rpc_a.ssl_context)
{
}

void nano::rpc_connection_secure::parse_connection ()
{
	// Perform the SSL handshake
	auto this_l = std::static_pointer_cast<nano::rpc_connection_secure> (shared_from_this ());
	stream.async_handshake (boost::asio::ssl::stream_base::server,
	[this_l](auto & ec) {
		this_l->handle_handshake (ec);
	});
}

void nano::rpc_connection_secure::on_shutdown (const boost::system::error_code & error)
{
	// No-op. We initiate the shutdown (since the RPC server kills the connection after each request)
	// and we'll thus get an expected EOF error. If the client disconnects, a short-read error will be expected.
}

void nano::rpc_connection_secure::handle_handshake (const boost::system::error_code & error)
{
	if (!error)
	{
		read ();
	}
	else
	{
		BOOST_LOG (node->log) << "TLS: Handshake error: " << error.message ();
	}
}

void nano::rpc_connection_secure::read ()
{
	auto this_l (std::static_pointer_cast<nano::rpc_connection_secure> (shared_from_this ()));
	boost::beast::http::async_read (stream, buffer, request, [this_l](boost::system::error_code const & ec, size_t bytes_transferred) {
		if (!ec)
		{
			this_l->node->background ([this_l]() {
				auto start (std::chrono::steady_clock::now ());
				auto version (this_l->request.version ());
				std::string request_id (boost::str (boost::format ("%1%") % boost::io::group (std::hex, std::showbase, reinterpret_cast<uintptr_t> (this_l.get ()))));
				auto response_handler ([this_l, version, start, request_id](boost::property_tree::ptree const & tree_a) {
					std::stringstream ostream;
					boost::property_tree::write_json (ostream, tree_a);
					ostream.flush ();
					auto body (ostream.str ());
					this_l->write_result (body, version);
					boost::beast::http::async_write (this_l->stream, this_l->res, [this_l](boost::system::error_code const & ec, size_t bytes_transferred) {
						// Perform the SSL shutdown
						this_l->stream.async_shutdown ([this_l](auto const & ec_shutdown) {
							this_l->on_shutdown (ec_shutdown);
						});
					});

					if (this_l->node->config.logging.log_rpc ())
					{
						BOOST_LOG (this_l->node->log) << boost::str (boost::format ("TLS: RPC request %2% completed in: %1% microseconds") % std::chrono::duration_cast<std::chrono::microseconds> (std::chrono::steady_clock::now () - start).count () % request_id);
					}
				});
				auto method = this_l->request.method ();
				switch (method)
				{
					case boost::beast::http::verb::post:
					{
						auto handler (std::make_shared<nano::rpc_handler> (*this_l->node, this_l->rpc, this_l->request.body (), request_id, response_handler));
						handler->process_request ();
						break;
					}
					case boost::beast::http::verb::options:
					{
						this_l->prepare_head (version);
						this_l->res.set (boost::beast::http::field::allow, "POST, OPTIONS");
						this_l->res.prepare_payload ();
						boost::beast::http::async_write (this_l->stream, this_l->res, [this_l](boost::system::error_code const & ec, size_t bytes_transferred) {

							// Perform the SSL shutdown
							this_l->stream.async_shutdown (
							std::bind (
							&nano::rpc_connection_secure::on_shutdown,
							this_l,
							std::placeholders::_1));

						});
						break;
					}
					default:
					{
						error_response (response_handler, "Can only POST requests");
						break;
					}
				}
			});
		}
		else
		{
			BOOST_LOG (this_l->node->log) << "TLS: Read error: " << ec.message () << std::endl;
		}
	});
}
