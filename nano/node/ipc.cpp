#include <algorithm>
#include <boost/array.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/thread/thread_time.hpp>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <nano/node/common.hpp>
#include <nano/node/ipc.hpp>
#include <nano/node/node.hpp>
#include <nano/node/rpc.hpp>
#include <thread>

using namespace boost::log;

std::string nano::error_ipc_messages::message (int ev) const
{
	switch (static_cast<nano::error_ipc> (ev))
	{
		case nano::error_ipc::generic:
			return "Unknown error";
		case nano::error_ipc::invalid_preamble:
			return "Invalid preamble";
	}
	return "Invalid error";
}
void nano::ipc::ipc_config::serialize_json (nano::jsonconfig & json)
{
	
}

bool nano::ipc::ipc_config::deserialize_json (nano::jsonconfig & tree_a)
{
	bool error = false;

	auto tcp_l (tree_a.get_optional_child ("tcp"));
	if (tcp_l)
	{
		tcp_l->get<size_t> ("io_threads", transport_tcp.io_threads);
		tcp_l->get<bool> ("enable", transport_tcp.enabled);
		tcp_l->get<std::string> ("address", transport_tcp.address);
		tcp_l->get<uint16_t> ("port", transport_tcp.port);
		tcp_l->get<size_t> ("io_timeout", transport_tcp.io_timeout);
	}

	auto domain_l (tree_a.get_optional_child ("local"));
	if (domain_l)
	{
		domain_l->get<size_t> ("io_threads", transport_domain.io_threads);
		domain_l->get<bool> ("enable", transport_domain.enabled);
		domain_l->get<std::string> ("path", transport_domain.path);
		domain_l->get<size_t> ("io_timeout", transport_domain.io_timeout);
	}

	return error;
}

/** A client session that manages its own lifetime */
template <typename SOCKET_TYPE>
class session : public std::enable_shared_from_this<session<SOCKET_TYPE>>
{
public:
	session (nano::ipc::ipc_server & server_a, boost::asio::io_context & io_context_a, nano::ipc::ipc_config_transport & config_transport_a) :
	server (server_a), node (server_a.node), io_context (io_context_a), socket (io_context_a), writer_strand (io_context_a), io_timer (io_context_a), config_transport (config_transport_a)
	{
	}

	~session ()
	{
		BOOST_LOG (node.log) << "IPC: session ended";
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
	void async_read_exactly (void * buff_a, size_t size_a, std::function<void()> fn)
	{
		async_read_exactly (buff_a, size_a, config_transport.io_timeout, fn);
	}

	/**
	 * Async read of exactly \p size bytes and a specific timeout.
	 * @see async_read_exactly (void *, size_t, std::function<void()>)
	 */
	void async_read_exactly (void * buff_a, size_t size, size_t timeout_seconds, std::function<void()> fn)
	{
		timer_start (timeout_seconds);
		boost::asio::async_read (socket,
		boost::asio::buffer (buff_a, size),
		boost::asio::transfer_exactly (size),
		boost::bind (&session::handle_read_or_error,
		this->shared_from_this (),
		fn,
		boost::asio::placeholders::error,
		boost::asio::placeholders::bytes_transferred));
	}

	void handle_read_or_error (std::function<void()> fn, const boost::system::error_code & error, size_t bytes_transferred)
	{
		timer_cancel ();
		if ((boost::asio::error::connection_aborted == error) || (boost::asio::error::connection_reset == error))
		{
			BOOST_LOG (node.log) << boost::str (boost::format ("IPC: error reading %1% ") % error.message ());
		}
		else if (bytes_transferred > 0)
		{
			fn ();
		}
	}

	/**
	 * Write callback. If no error occurs, the session starts waiting for another request.
	 * Clients are expected to implement reconnect logic.
	 */
	void handle_write (const boost::system::error_code & error, size_t bytes_transferred)
	{
		timer_cancel ();
		if (!error)
		{
			read_next_request ();
		}
		else
		{
			BOOST_LOG (node.log) << "IPC: Write failed: " << error.message ();
		}
	}

	/** Handler for payloads of type nano::ipc_encoding::json_legacy */
	void rpc_handle_query ()
	{
		auto start (std::chrono::steady_clock::now ());
		request_id_str = (boost::str (boost::format ("%1%") % boost::io::group (std::hex, std::showbase, request_id.fetch_add (1))));

		// This is called when the nano::rpc_handler#process_request is done. We convert to
		// json and writes the response to the ipc socket with a length prefix.
		auto self_l (this->shared_from_this ());
		auto response_handler ([self_l, start](boost::property_tree::ptree const & tree_a) {
			std::stringstream ostream;
			boost::property_tree::write_json (ostream, tree_a);
			ostream.flush ();
			self_l->request_body = ostream.str ();

			uint32_t size_response = boost::endian::native_to_big ((uint32_t)self_l->request_body.size ());
			std::vector<boost::asio::mutable_buffer> bufs = {
				boost::asio::buffer (&size_response, 4),
				boost::asio::buffer (self_l->request_body)
			};

			self_l->timer_start (self_l->config_transport.io_timeout);
			boost::asio::async_write (self_l->socket, bufs,
			self_l->writer_strand.wrap (
			boost::bind (
			&session::handle_write,
			self_l,
			boost::asio::placeholders::error,
			boost::asio::placeholders::bytes_transferred)));

			if (self_l->node.config.logging.log_rpc ())
			{
				BOOST_LOG (self_l->node.log) << boost::str (boost::format ("IPC/RPC request %1% completed in: %2% microseconds") % self_l->request_id_str % std::chrono::duration_cast<std::chrono::microseconds> (std::chrono::steady_clock::now () - start).count ());
			}
		});

		node.stats.inc (nano::stat::type::ipc, nano::stat::detail::invocations);
		auto body (std::string ((char *)buffer.data (), buffer.size ()));

		// Note that if the rpc action is async, the shared_ptr<rpc_handler> lifetime will be extended by the action handler
		auto handler (std::make_shared<nano::rpc_handler> (node, server.rpc, body, request_id_str, response_handler));
		handler->process_request ();
	}

	/** Async request reader */
	void read_next_request ()
	{
		auto this_l = this->shared_from_this ();

		// Await preamble
		buffer.resize (4);
		async_read_exactly (buffer.data (), buffer.size (), std::numeric_limits<size_t>::max (), [this_l]() {
			if (this_l->buffer[0] != 'N')
			{
				BOOST_LOG (this_l->node.log) << "IPC: Invalid preamble";
			}
			else if (this_l->buffer[1] == static_cast<uint8_t> (nano::ipc_encoding::json_legacy))
			{
				// Length of query
				this_l->async_read_exactly (&this_l->buffer_size, 4, [this_l]() {
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

	void close ()
	{
		socket.shutdown (boost::asio::ip::tcp::socket::shutdown_both);
		socket.close ();
	}

protected:
	/**
	 * Start IO timer.
	 * @param sec Seconds to wait. To wait indefinitely, use std::numeric_limits<size_t>::max().
	 */
	void timer_start (size_t sec)
	{
		if (sec < std::numeric_limits<size_t>::max ())
		{
			io_timer.expires_from_now (boost::posix_time::seconds (sec));
			io_timer.async_wait ([this](const boost::system::error_code & ec) {
				if (!ec)
				{
					this->timer_expired ();
				}
			});
		}
	}

	void timer_expired (void)
	{
		close ();
		BOOST_LOG (node.log) << "IPC: IO timeout";
	}

	void timer_cancel (void)
	{
		boost::system::error_code ec;
		this->io_timer.cancel (ec);
	}

private:
	nano::ipc::ipc_server & server;
	nano::node & node;

	// Request data for ipc_encoding::json_legacy payloads
	std::atomic<int> request_id{ 0 };
	std::string request_body;
	std::string request_id_str;

	/**
	 * IO context from node, or per-transport, depending on configuration.
	 * Certain transport configurations (tcp+ssl) may scale better if they use a
	 * separate context.
	 */
	boost::asio::io_context & io_context;

	/** A socket of the given asio type */
	SOCKET_TYPE socket;

	/**
	 * Allow multiple threads to write simultaniously. This allows for future extensions like
	 * async callback feeds without locking.
	 */
	boost::asio::io_context::strand writer_strand;

	/** Receives the size of header/query payloads */
	uint32_t buffer_size = 0;

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
	socket_transport (nano::ipc::ipc_server & server, ENDPOINT_TYPE ep, nano::ipc::ipc_config_transport & config_transport_a, int concurrency) :
	server (server), config_transport (config_transport_a)
	{
		// Using a per-transport event dispatcher?
		if (concurrency > 0)
		{
			io_context = std::make_unique<boost::asio::io_context> ();
		}

		boost::asio::socket_base::reuse_address option (true);
		boost::asio::socket_base::keep_alive option_keepalive (true);
		acceptor = std::make_unique<ACCEPTOR_TYPE> (context (), ep);
		acceptor->set_option (option);
		acceptor->set_option (option_keepalive);
		accept ();

		// Start serving IO requests. If concurrency is 0, the node's thread pool is used instead.
		if (concurrency > 0)
		{
			runner = std::make_unique<nano::thread_runner> (*io_context, concurrency);
		}
	}

	boost::asio::io_context & context () const
	{
		return io_context ? *io_context : server.node.io_ctx;
	}

	void accept ()
	{
		std::shared_ptr<session<SOCKET_TYPE>> new_session (new session<SOCKET_TYPE> (server, io_context ? *io_context : server.node.io_ctx, config_transport));

		acceptor->async_accept (new_session->get_socket (),
		boost::bind (&socket_transport::handle_accept, this, new_session,
		boost::asio::placeholders::error));
	}

	void handle_accept (std::shared_ptr<session<SOCKET_TYPE>> new_session, const boost::system::error_code & error)
	{
		if (!error)
		{
			new_session->read_next_request ();
		}
		else
		{
			BOOST_LOG (server.node.log) << "IPC acceptor error: " << error.message ();
		}

		if (acceptor->is_open ())
		{
			accept ();
		}
	}

	void stop ()
	{
		acceptor->close ();
		io_context->stop ();

		if (runner)
		{
			runner->join ();
		}
	}

private:
	nano::ipc::ipc_server & server;
	nano::ipc::ipc_config_transport & config_transport;
	std::unique_ptr<nano::thread_runner> runner;
	std::unique_ptr<boost::asio::io_context> io_context;
	std::unique_ptr<ACCEPTOR_TYPE> acceptor;
};

/** Domain socket file remover */
class nano::ipc::dsock_file_remover
{
public:
	dsock_file_remover (std::string file) :
	filename (file)
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
