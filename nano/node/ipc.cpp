#include <algorithm>
#include <boost/array.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/thread/thread_time.hpp>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <nano/lib/timer.hpp>
#include <nano/node/common.hpp>
#include <nano/node/ipc.hpp>
#include <nano/node/node.hpp>
#include <nano/node/rpc.hpp>
#include <thread>

using namespace boost::log;

std::string nano::error_ipc_messages::message (int error_code_a) const
{
	switch (static_cast<nano::error_ipc> (error_code_a))
	{
		case nano::error_ipc::generic:
			return "Unknown error";
		case nano::error_ipc::invalid_preamble:
			return "Invalid preamble";
	}
	return "Invalid error";
}
nano::error nano::ipc::ipc_config::serialize_json (nano::jsonconfig & json) const
{
	nano::jsonconfig tcp_l;
	tcp_l.put ("io_threads", transport_tcp.io_threads);
	tcp_l.put ("enable", transport_tcp.enabled);
	tcp_l.put ("address", transport_tcp.address);
	tcp_l.put ("port", transport_tcp.port);
	tcp_l.put ("io_timeout", transport_tcp.io_timeout);
	json.put_child ("tcp", tcp_l);

	nano::jsonconfig domain_l;
	domain_l.put ("io_threads", transport_domain.io_threads);
	domain_l.put ("enable", transport_domain.enabled);
	domain_l.put ("path", transport_domain.path);
	domain_l.put ("io_timeout", transport_domain.io_timeout);
	json.put_child ("domain", domain_l);
	return json.get_error ();
}

nano::error nano::ipc::ipc_config::deserialize_json (nano::jsonconfig & json)
{
	auto tcp_l (json.get_optional_child ("tcp"));
	if (tcp_l)
	{
		tcp_l->get<size_t> ("io_threads", transport_tcp.io_threads);
		tcp_l->get<bool> ("enable", transport_tcp.enabled);
		tcp_l->get<std::string> ("address", transport_tcp.address);
		tcp_l->get<uint16_t> ("port", transport_tcp.port);
		tcp_l->get<size_t> ("io_timeout", transport_tcp.io_timeout);
	}

	auto domain_l (json.get_optional_child ("local"));
	if (domain_l)
	{
		domain_l->get<size_t> ("io_threads", transport_domain.io_threads);
		domain_l->get<bool> ("enable", transport_domain.enabled);
		domain_l->get<std::string> ("path", transport_domain.path);
		domain_l->get<size_t> ("io_timeout", transport_domain.io_timeout);
	}

	return json.get_error ();
}

/**
 * A session represents a client connection over which multiple requests/reponses are transmittet.
 */
template <typename SOCKET_TYPE>
class session : public std::enable_shared_from_this<session<SOCKET_TYPE>>
{
public:
	session (nano::ipc::ipc_server & server_a, boost::asio::io_context & io_context_a, nano::ipc::ipc_config_transport & config_transport_a) :
	server (server_a), node (server_a.node), session_id (server_a.id_dispenser.fetch_add (1)), io_context (io_context_a), socket (io_context_a), io_timer (io_context_a), config_transport (config_transport_a)
	{
		if (node.config.logging.log_rpc ())
		{
			BOOST_LOG (node.log) << "IPC: created session with id: " << session_id;
		}
	}

	~session ()
	{
		if (node.config.logging.log_rpc ())
		{
			BOOST_LOG (node.log) << "IPC: ended session with id: " << session_id;
		}
	}

	SOCKET_TYPE & get_socket ()
	{
		return socket;
	}

	/**
	 * Async read of exactly \p size bytes. The callback is called only when all the data is available and
	 * no error has occurred. On error, the error is logged, the read cycle stops and the session ends. Clients
	 * are expected to implement reconnect logic.
	 */
	void async_read_exactly (void * buff_a, size_t size_a, std::function<void()> callback_a)
	{
		async_read_exactly (buff_a, size_a, std::chrono::seconds (config_transport.io_timeout), callback_a);
	}

	/**
	 * Async read of exactly \p size bytes and a specific timeout.
	 * @see async_read_exactly (void *, size_t, std::function<void()>)
	 */
	void async_read_exactly (void * buff_a, size_t size_a, std::chrono::seconds timeout_a, std::function<void()> callback_a)
	{
		timer_start (timeout_a);

		auto this_l (this->shared_from_this ());
		boost::asio::async_read (socket,
		boost::asio::buffer (buff_a, size_a),
		boost::asio::transfer_exactly (size_a),
		[this_l, callback_a](boost::system::error_code const & ec, size_t bytes_transferred_a) {
			if (ec == boost::asio::error::connection_aborted || ec == boost::asio::error::connection_reset)
			{
				BOOST_LOG (this_l->node.log) << boost::str (boost::format ("IPC: error reading %1% ") % ec.message ());
			}
			else if (bytes_transferred_a > 0)
			{
				callback_a ();
			}
		});
	}

	/**
	 * Write callback. If no error occurs, the session starts waiting for another request.
	 * Clients are expected to implement reconnect logic.
	 */
	void handle_write (const boost::system::error_code & error_a, size_t bytes_transferred_a)
	{
		timer_cancel ();
		if (!error_a)
		{
			read_next_request ();
		}
		else
		{
			BOOST_LOG (node.log) << "IPC: Write failed: " << error_a.message ();
		}
	}

	/** Handler for payloads of type nano::ipc_encoding::json_legacy */
	void rpc_handle_query ()
	{
		session_timer.restart ();
		auto request_id_l (std::to_string (server.id_dispenser.fetch_add (1)));

		// This is called when the nano::rpc_handler#process_request is done. We convert to
		// json and write the response to the ipc socket with a length prefix.
		auto this_l (this->shared_from_this ());
		auto response_handler_l ([this_l, request_id_l](boost::property_tree::ptree const & tree_a) {
			std::stringstream ostream;
			boost::property_tree::write_json (ostream, tree_a);
			ostream.flush ();
			std::string request_body = ostream.str ();

			uint32_t size_response = boost::endian::native_to_big ((uint32_t)request_body.size ());
			std::vector<boost::asio::mutable_buffer> bufs = {
				boost::asio::buffer (&size_response, sizeof (size_response)),
				boost::asio::buffer (request_body)
			};

			this_l->timer_start (std::chrono::seconds (this_l->config_transport.io_timeout));
			boost::asio::async_write (this_l->socket, bufs, [this_l](boost::system::error_code const & error_a, size_t size_a) {
				this_l->timer_cancel ();
				if (!error_a)
				{
					this_l->read_next_request ();
				}
				else
				{
					BOOST_LOG (this_l->node.log) << "IPC: Write failed: " << error_a.message ();
				}
			});

			if (this_l->node.config.logging.log_rpc ())
			{
				BOOST_LOG (this_l->node.log) << boost::str (boost::format ("IPC/RPC request %1% completed in: %2% %3%") % request_id_l % this_l->session_timer.stop ().count () % this_l->session_timer.unit ());
			}
		});

		node.stats.inc (nano::stat::type::ipc, nano::stat::detail::invocations);
		auto body (std::string ((char *)buffer.data (), buffer.size ()));

		// Note that if the rpc action is async, the shared_ptr<rpc_handler> lifetime will be extended by the action handler
		nano::rpc_handler handler (node, server.rpc, body, request_id_l, response_handler_l);
		handler.process_request ();
	}

	/** Async request reader */
	void read_next_request ()
	{
		auto this_l = this->shared_from_this ();

		// Await next request indefinitely.
		// The request format is four bytes; ['N', payload-type, reserved, reserved]
		buffer.resize (sizeof (buffer_size));
		async_read_exactly (buffer.data (), buffer.size (), std::chrono::seconds::max (), [this_l]() {
			if (this_l->buffer[0] != 'N')
			{
				BOOST_LOG (this_l->node.log) << "IPC: Invalid preamble";
			}
			else if (this_l->buffer[1] == static_cast<uint8_t> (nano::ipc_encoding::json_legacy))
			{
				// Length of query
				this_l->async_read_exactly (&this_l->buffer_size, sizeof (this_l->buffer_size), [this_l]() {
					boost::endian::big_to_native_inplace (this_l->buffer_size);
					this_l->buffer.resize (this_l->buffer_size);
					// Query
					this_l->async_read_exactly (this_l->buffer.data (), this_l->buffer_size, [this_l]() {
						this_l->rpc_handle_query ();
					});
				});
			}
			else
			{
				BOOST_LOG (this_l->node.log) << "IPC: Unsupported payload encoding";
			}
		});
	}

	/** Shut down and close socket */
	void close ()
	{
		socket.shutdown (boost::asio::ip::tcp::socket::shutdown_both);
		socket.close ();
	}

protected:
	/**
	 * Start IO timer.
	 * @param timeout_a Seconds to wait. To wait indefinitely, use std::chrono::seconds::max ()
	 */
	void timer_start (std::chrono::seconds timeout_a)
	{
		if (timeout_a < std::chrono::seconds::max ())
		{
			io_timer.expires_from_now (boost::posix_time::seconds (timeout_a.count ()));
			io_timer.async_wait ([this](const boost::system::error_code & ec) {
				if (!ec)
				{
					this->timer_expired ();
				}
			});
		}
	}

	void timer_expired ()
	{
		close ();
		BOOST_LOG (node.log) << "IPC: IO timeout";
	}

	void timer_cancel ()
	{
		boost::system::error_code ec;
		this->io_timer.cancel (ec);
	}

private:
	nano::ipc::ipc_server & server;
	nano::node & node;

	/** Unique session id used for logging */
	uint64_t session_id;

	/** Timer for measuring operations */
	nano::timer<std::chrono::microseconds> session_timer;

	/**
	 * IO context from node, or per-transport, depending on configuration.
	 * Certain transport configurations (tcp+ssl) may scale better if they use a
	 * separate context.
	 */
	boost::asio::io_context & io_context;

	/** A socket of the given asio type */
	SOCKET_TYPE socket;

	/** Buffer sizes are read into this */
	uint32_t buffer_size{ 0 };

	/** Buffer used to store data received from the client */
	std::vector<uint8_t> buffer;

	/** IO operation timer */
	boost::asio::deadline_timer io_timer;

	/** Transport configuration */
	nano::ipc::ipc_config_transport & config_transport;
};

/** Domain and TCP socket transport */
template <typename ACCEPTOR_TYPE, typename SOCKET_TYPE, typename ENDPOINT_TYPE>
class socket_transport : public nano::ipc::transport
{
public:
	socket_transport (nano::ipc::ipc_server & server_a, ENDPOINT_TYPE endpoint_a, nano::ipc::ipc_config_transport & config_transport_a, int concurrency_a) :
	server (server_a), config_transport (config_transport_a)
	{
		// Using a per-transport event dispatcher?
		if (concurrency_a > 0)
		{
			io_ctx = std::make_unique<boost::asio::io_context> ();
		}

		boost::asio::socket_base::reuse_address option (true);
		boost::asio::socket_base::keep_alive option_keepalive (true);
		acceptor = std::make_unique<ACCEPTOR_TYPE> (context (), endpoint_a);
		acceptor->set_option (option);
		acceptor->set_option (option_keepalive);
		accept ();

		// Start serving IO requests. If concurrency_a is 0, the node's thread pool/io_context is used instead.
		// A separate io_context for domain sockets may facilitate better performance on some systems.
		if (concurrency_a > 0)
		{
			runner = std::make_unique<nano::thread_runner> (*io_ctx, concurrency_a);
		}
	}

	boost::asio::io_context & context () const
	{
		return io_ctx ? *io_ctx : server.node.io_ctx;
	}

	void accept ()
	{
		// Prepare the next session
		auto new_session (std::make_shared<session<SOCKET_TYPE>> (server, context (), config_transport));

		acceptor->async_accept (new_session->get_socket (), [this, new_session](boost::system::error_code const & ec) {
			if (!ec)
			{
				new_session->read_next_request ();
			}
			else
			{
				BOOST_LOG (server.node.log) << "IPC acceptor error: " << ec.message ();
			}

			if (acceptor->is_open () && ec != boost::asio::error::operation_aborted)
			{
				this->accept ();
			}
		});
	}

	void stop ()
	{
		acceptor->close ();
		io_ctx->stop ();

		if (runner)
		{
			runner->join ();
		}
	}

private:
	nano::ipc::ipc_server & server;
	nano::ipc::ipc_config_transport & config_transport;
	std::unique_ptr<nano::thread_runner> runner;
	std::unique_ptr<boost::asio::io_context> io_ctx;
	std::unique_ptr<ACCEPTOR_TYPE> acceptor;
};

/** Domain socket file remover */
class nano::ipc::dsock_file_remover
{
public:
	dsock_file_remover (std::string file_a) :
	filename (file_a)
	{
		std::remove (filename.c_str ());
	}
	~dsock_file_remover ()
	{
		std::remove (filename.c_str ());
	}
	std::string filename;
};

nano::ipc::ipc_server::ipc_server (nano::node & node_a, nano::rpc & rpc_a) :
node (node_a), rpc (rpc_a),
stopped (false)
{
	try
	{
		if (node_a.config.ipc_config.transport_domain.enabled)
		{
#if defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)
			size_t threads = node_a.config.ipc_config.transport_domain.io_threads;
			file_remover = std::make_unique<dsock_file_remover> (node_a.config.ipc_config.transport_domain.path);
			boost::asio::local::stream_protocol::endpoint ep{ node_a.config.ipc_config.transport_domain.path };
			transports.push_back (std::make_shared<socket_transport<boost::asio::local::stream_protocol::acceptor, boost::asio::local::stream_protocol::socket, boost::asio::local::stream_protocol::endpoint>> (*this, ep, node_a.config.ipc_config.transport_domain, threads));
#else
			BOOST_LOG (node.log) << "IPC: Domain sockets are not supported on this platform";
#endif
		}

		if (node_a.config.ipc_config.transport_tcp.enabled)
		{
			size_t threads = node_a.config.ipc_config.transport_tcp.io_threads;
			transports.push_back (std::make_shared<socket_transport<boost::asio::ip::tcp::acceptor, boost::asio::ip::tcp::socket, boost::asio::ip::tcp::endpoint>> (*this, boost::asio::ip::tcp::endpoint (boost::asio::ip::tcp::v6 (), node_a.config.ipc_config.transport_tcp.port), node_a.config.ipc_config.transport_domain, threads));
		}

		BOOST_LOG (node.log) << "IPC: server started";
	}
	catch (std::runtime_error const & ex)
	{
		BOOST_LOG (node.log) << "IPC: " << ex.what ();
	}
}

nano::ipc::ipc_server::~ipc_server ()
{
	BOOST_LOG (node.log) << "IPC: server stopped";
}

void nano::ipc::ipc_server::stop ()
{
	for (auto & transport : transports)
	{
		transport->stop ();
	}
	stopped = true;
}
