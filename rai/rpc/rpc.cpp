#include <boost/algorithm/string.hpp>
#include <rai/rpc/rpc.hpp>
#include <rai/rpc/rpc_handler.hpp>

#include <rai/lib/interface.h>
#include <rai/node/node.hpp>

#ifdef RAIBLOCKS_SECURE_RPC
#include <rai/rpc/rpc_secure.hpp>
#endif

#include <rai/lib/errors.hpp>

rai::rpc_secure_config::rpc_secure_config () :
enable (false),
verbose_logging (false)
{
}

void rai::rpc_secure_config::serialize_json (boost::property_tree::ptree & tree_a) const
{
	tree_a.put ("enable", enable);
	tree_a.put ("verbose_logging", verbose_logging);
	tree_a.put ("server_key_passphrase", server_key_passphrase);
	tree_a.put ("server_cert_path", server_cert_path);
	tree_a.put ("server_key_path", server_key_path);
	tree_a.put ("server_dh_path", server_dh_path);
	tree_a.put ("client_certs_path", client_certs_path);
}

bool rai::rpc_secure_config::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto error (false);
	try
	{
		enable = tree_a.get<bool> ("enable");
		verbose_logging = tree_a.get<bool> ("verbose_logging");
		server_key_passphrase = tree_a.get<std::string> ("server_key_passphrase");
		server_cert_path = tree_a.get<std::string> ("server_cert_path");
		server_key_path = tree_a.get<std::string> ("server_key_path");
		server_dh_path = tree_a.get<std::string> ("server_dh_path");
		client_certs_path = tree_a.get<std::string> ("client_certs_path");
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

rai::rpc_config::rpc_config () :
address (boost::asio::ip::address_v6::loopback ()),
port (rai::rpc::rpc_port),
enable_control (false),
frontier_request_limit (16384),
chain_request_limit (16384),
max_json_depth (20)
{
}

rai::rpc_config::rpc_config (bool enable_control_a) :
address (boost::asio::ip::address_v6::loopback ()),
port (rai::rpc::rpc_port),
enable_control (enable_control_a),
frontier_request_limit (16384),
chain_request_limit (16384),
max_json_depth (20)
{
}

void rai::rpc_config::serialize_json (boost::property_tree::ptree & tree_a) const
{
	tree_a.put ("address", address.to_string ());
	tree_a.put ("port", std::to_string (port));
	tree_a.put ("enable_control", enable_control);
	tree_a.put ("frontier_request_limit", frontier_request_limit);
	tree_a.put ("chain_request_limit", chain_request_limit);
	tree_a.put ("max_json_depth", max_json_depth);
}

bool rai::rpc_config::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto result (false);
	try
	{
		auto rpc_secure_l (tree_a.get_child_optional ("secure"));
		if (rpc_secure_l)
		{
			result = secure.deserialize_json (rpc_secure_l.get ());
		}

		if (!result)
		{
			auto address_l (tree_a.get<std::string> ("address"));
			auto port_l (tree_a.get<std::string> ("port"));
			enable_control = tree_a.get<bool> ("enable_control");
			auto frontier_request_limit_l (tree_a.get<std::string> ("frontier_request_limit"));
			auto chain_request_limit_l (tree_a.get<std::string> ("chain_request_limit"));
			max_json_depth = tree_a.get<uint8_t> ("max_json_depth", max_json_depth);
			try
			{
				port = std::stoul (port_l);
				result = port > std::numeric_limits<uint16_t>::max ();
				frontier_request_limit = std::stoull (frontier_request_limit_l);
				chain_request_limit = std::stoull (chain_request_limit_l);
			}
			catch (std::logic_error const &)
			{
				result = true;
			}
			boost::system::error_code ec;
			address = boost::asio::ip::address_v6::from_string (address_l, ec);
			if (ec)
			{
				result = true;
			}
		}
	}
	catch (std::runtime_error const &)
	{
		result = true;
	}
	return result;
}

rai::rpc::rpc (boost::asio::io_service & service_a, rai::node & node_a, rai::rpc_config const & config_a) :
acceptor (service_a),
config (config_a),
node (node_a)
{
}

void rai::rpc::start ()
{
	auto endpoint (rai::tcp_endpoint (config.address, config.port));
	acceptor.open (endpoint.protocol ());
	acceptor.set_option (boost::asio::ip::tcp::acceptor::reuse_address (true));

	boost::system::error_code ec;
	acceptor.bind (endpoint, ec);
	if (ec)
	{
		BOOST_LOG (node.log) << boost::str (boost::format ("Error while binding for RPC on port %1%: %2%") % endpoint.port () % ec.message ());
		throw std::runtime_error (ec.message ());
	}

	acceptor.listen ();
	node.observers.blocks.add ([this](std::shared_ptr<rai::block> block_a, rai::account const & account_a, rai::uint128_t const &, bool) {
		observer_action (account_a);
	});

	accept ();
}

void rai::rpc::accept ()
{
	auto connection (std::make_shared<rai::rpc_connection> (node, *this));
	acceptor.async_accept (connection->socket, [this, connection](boost::system::error_code const & ec) {
		if (!ec)
		{
			accept ();
			connection->parse_connection ();
		}
		else
		{
			BOOST_LOG (this->node.log) << boost::str (boost::format ("Error accepting RPC connections: %1%") % ec);
		}
	});
}

void rai::rpc::stop ()
{
	acceptor.close ();
}

rai::rpc_connection::rpc_connection (rai::node & node_a, rai::rpc & rpc_a) :
node (node_a.shared ()),
rpc (rpc_a),
socket (node_a.service)
{
	responded.clear ();
}

void rai::rpc_connection::parse_connection ()
{
	read ();
}

void rai::rpc_connection::write_result (std::string body, unsigned version)
{
	if (!responded.test_and_set ())
	{
		res.set ("Content-Type", "application/json");
		res.set ("Access-Control-Allow-Origin", "*");
		res.set ("Access-Control-Allow-Headers", "Accept, Accept-Language, Content-Language, Content-Type");
		res.set ("Connection", "close");
		res.result (boost::beast::http::status::ok);
		res.body () = body;
		res.version (version);
		res.prepare_payload ();
	}
	else
	{
		assert (false && "RPC already responded and should only respond once");
		// Guards `res' from being clobbered while async_write is being serviced
	}
}

void rai::rpc_connection::read ()
{
	auto this_l (shared_from_this ());
	boost::beast::http::async_read (socket, buffer, request, [this_l](boost::system::error_code const & ec, size_t bytes_transferred) {
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
					boost::beast::http::async_write (this_l->socket, this_l->res, [this_l](boost::system::error_code const & ec, size_t bytes_transferred) {
					});

					if (this_l->node->config.logging.log_rpc ())
					{
						BOOST_LOG (this_l->node->log) << boost::str (boost::format ("RPC request %2% completed in: %1% microseconds") % std::chrono::duration_cast<std::chrono::microseconds> (std::chrono::steady_clock::now () - start).count () % request_id);
					}
				});
				if (this_l->request.method () == boost::beast::http::verb::post)
				{
					auto handler (std::make_shared<rai::rpc_handler> (*this_l->node, this_l->rpc, this_l->request.body (), request_id, response_handler));
					handler->process_request ();
				}
				else
				{
					error_response (response_handler, "Can only POST requests");
				}
			});
		}
		else
		{
			BOOST_LOG (this_l->node->log) << "RPC read error: " << ec.message ();
		}
	});
}

rai::payment_observer::payment_observer (std::function<void(boost::property_tree::ptree const &)> const & response_a, rai::rpc & rpc_a, rai::account const & account_a, rai::amount const & amount_a) :
rpc (rpc_a),
account (account_a),
amount (amount_a),
response (response_a)
{
	completed.clear ();
}

void rai::payment_observer::start (uint64_t timeout)
{
	auto this_l (shared_from_this ());
	rpc.node.alarm.add (std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout), [this_l]() {
		this_l->complete (rai::payment_status::nothing);
	});
}

rai::payment_observer::~payment_observer ()
{
}

void rai::payment_observer::observe ()
{
	if (rpc.node.balance (account) >= amount.number ())
	{
		complete (rai::payment_status::success);
	}
}

void rai::payment_observer::complete (rai::payment_status status)
{
	auto already (completed.test_and_set ());
	if (!already)
	{
		if (rpc.node.config.logging.log_rpc ())
		{
			BOOST_LOG (rpc.node.log) << boost::str (boost::format ("Exiting payment_observer for account %1% status %2%") % account.to_account () % static_cast<unsigned> (status));
		}
		switch (status)
		{
			case rai::payment_status::nothing:
			{
				boost::property_tree::ptree response_l;
				response_l.put ("status", "nothing");
				response (response_l);
				break;
			}
			case rai::payment_status::success:
			{
				boost::property_tree::ptree response_l;
				response_l.put ("status", "success");
				response (response_l);
				break;
			}
			default:
			{
				error_response (response, "Internal payment error");
				break;
			}
		}
		std::lock_guard<std::mutex> lock (rpc.mutex);
		assert (rpc.payment_observers.find (account) != rpc.payment_observers.end ());
		rpc.payment_observers.erase (account);
	}
}

std::unique_ptr<rai::rpc> rai::get_rpc (boost::asio::io_service & service_a, rai::node & node_a, rai::rpc_config const & config_a)
{
	std::unique_ptr<rpc> impl;

	if (config_a.secure.enable)
	{
#ifdef RAIBLOCKS_SECURE_RPC
		impl.reset (new rpc_secure (service_a, node_a, config_a));
#else
		std::cerr << "RPC configured for TLS, but the node is not compiled with TLS support" << std::endl;
#endif
	}
	else
	{
		impl.reset (new rpc (service_a, node_a, config_a));
	}

	return impl;
}
