#include <algorithm>
#include <boost/array.hpp>
#include <boost/bind.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/polymorphic_cast.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/thread/thread_time.hpp>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <future>
#include <iostream>
#include <nano/lib/timer.hpp>
#include <nano/node/common.hpp>
#include <nano/node/ipc.hpp>
#include <nano/node/node.hpp>
#include <nano/node/rpc.hpp>
#include <thread>

using namespace boost::log;
namespace
{
/**
 * The IPC framing format is simple: preamble followed by an encoding specific payload.
 * Preamble is uint8_t {'N', encoding_type, reserved, reserved}. Reserved bytes MUST be zero.
 * @note This is intentionally not an enum class as the values are only used as vector indices.
 */
enum preamble_offset
{
	/** Always 'N' */
	lead = 0,
	/** One of the payload_encoding values */
	encoding = 1,
	/** Always zero */
	reserved_1 = 2,
	/** Always zero */
	reserved_2 = 3,
};
}
nano::error nano::ipc::ipc_config::serialize_json (nano::jsonconfig & json) const
{
	nano::jsonconfig tcp_l;
	// Only write out experimental config values if they're previously set explicitly in the config file
	if (transport_tcp.io_threads >= 0)
	{
		tcp_l.put ("io_threads", transport_tcp.io_threads);
	}
	tcp_l.put ("enable", transport_tcp.enabled);
	tcp_l.put ("port", transport_tcp.port);
	tcp_l.put ("io_timeout", transport_tcp.io_timeout);
	json.put_child ("tcp", tcp_l);

	nano::jsonconfig domain_l;
	if (transport_domain.io_threads >= 0)
	{
		domain_l.put ("io_threads", transport_domain.io_threads);
	}
	domain_l.put ("enable", transport_domain.enabled);
	domain_l.put ("path", transport_domain.path);
	domain_l.put ("io_timeout", transport_domain.io_timeout);
	json.put_child ("local", domain_l);
	return json.get_error ();
}

nano::error nano::ipc::ipc_config::deserialize_json (nano::jsonconfig & json)
{
	auto tcp_l (json.get_optional_child ("tcp"));
	if (tcp_l)
	{
		tcp_l->get_optional<long> ("io_threads", transport_tcp.io_threads, -1);
		tcp_l->get<bool> ("enable", transport_tcp.enabled);
		tcp_l->get<uint16_t> ("port", transport_tcp.port);
		tcp_l->get<size_t> ("io_timeout", transport_tcp.io_timeout);
	}

	auto domain_l (json.get_optional_child ("local"));
	if (domain_l)
	{
		domain_l->get_optional<long> ("io_threads", transport_domain.io_threads, -1);
		domain_l->get<bool> ("enable", transport_domain.enabled);
		domain_l->get<std::string> ("path", transport_domain.path);
		domain_l->get<size_t> ("io_timeout", transport_domain.io_timeout);
	}

	return json.get_error ();
}

/** Abstract base type for sockets, implementing timer logic and a close operation */
class socket_base
{
public:
	socket_base (boost::asio::io_context & io_ctx_a) :
	io_timer (io_ctx_a)
	{
	}
	virtual ~socket_base () = default;

	/** Close socket */
	virtual void close () = 0;

	/**
	 * Start IO timer.
	 * @param timeout_a Seconds to wait. To wait indefinitely, use std::chrono::seconds::max ()
	 */
	void timer_start (std::chrono::seconds timeout_a)
	{
		if (timeout_a < std::chrono::seconds::max ())
		{
			io_timer.expires_from_now (boost::posix_time::seconds (static_cast<long> (timeout_a.count ())));
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
	}

	void timer_cancel ()
	{
		boost::system::error_code ec;
		io_timer.cancel (ec);
		assert (!ec);
	}

private:
	/** IO operation timer */
	boost::asio::deadline_timer io_timer;
};

/**
 * A session represents an inbound connection over which multiple requests/reponses are transmitted.
 */
template <typename SOCKET_TYPE>
class session : public socket_base, public std::enable_shared_from_this<session<SOCKET_TYPE>>
{
public:
	session (nano::ipc::ipc_server & server_a, boost::asio::io_context & io_ctx_a, nano::ipc::ipc_config_transport & config_transport_a) :
	socket_base (io_ctx_a),
	server (server_a), node (server_a.node), session_id (server_a.id_dispenser.fetch_add (1)), io_ctx (io_ctx_a), socket (io_ctx_a), config_transport (config_transport_a)
	{
		if (node.config.logging.log_ipc ())
		{
			BOOST_LOG (node.log) << "IPC: created session with id: " << session_id;
		}
	}

	SOCKET_TYPE & get_socket ()
	{
		return socket;
	}

	/**
	 * Async read of exactly \p size_a bytes. The callback is invoked only when all the data is available and
	 * no error has occurred. On error, the error is logged, the read cycle stops and the session ends. Clients
	 * are expected to implement reconnect logic.
	 */
	void async_read_exactly (void * buff_a, size_t size_a, std::function<void()> callback_a)
	{
		async_read_exactly (buff_a, size_a, std::chrono::seconds (config_transport.io_timeout), callback_a);
	}

	/**
	 * Async read of exactly \p size_a bytes and a specific \p timeout_a.
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
			this_l->timer_cancel ();
			if (ec == boost::asio::error::connection_aborted || ec == boost::asio::error::connection_reset)
			{
				if (this_l->node.config.logging.log_ipc ())
				{
					BOOST_LOG (this_l->node.log) << boost::str (boost::format ("IPC: error reading %1% ") % ec.message ());
				}
			}
			else if (bytes_transferred_a > 0)
			{
				callback_a ();
			}
		});
	}

	/** Handler for payload_encoding::json_legacy */
	void rpc_handle_query ()
	{
		session_timer.restart ();
		auto request_id_l (std::to_string (server.id_dispenser.fetch_add (1)));

		// This is called when nano::rpc_handler#process_request is done. We convert to
		// json and write the response to the ipc socket with a length prefix.
		auto this_l (this->shared_from_this ());
		auto response_handler_l ([this_l, request_id_l](boost::property_tree::ptree const & tree_a) {
			std::stringstream ostream;
			boost::property_tree::write_json (ostream, tree_a);
			ostream.flush ();
			this_l->response_body = ostream.str ();

			uint32_t size_response = boost::endian::native_to_big (static_cast<uint32_t> (this_l->response_body.size ()));
			std::vector<boost::asio::mutable_buffer> bufs = {
				boost::asio::buffer (&size_response, sizeof (size_response)),
				boost::asio::buffer (this_l->response_body)
			};

			this_l->timer_start (std::chrono::seconds (this_l->config_transport.io_timeout));
			boost::asio::async_write (this_l->socket, bufs, [this_l](boost::system::error_code const & error_a, size_t size_a) {
				this_l->timer_cancel ();
				if (!error_a)
				{
					this_l->read_next_request ();
				}
				else if (this_l->node.config.logging.log_ipc ())
				{
					BOOST_LOG (this_l->node.log) << "IPC: Write failed: " << error_a.message ();
				}
			});

			if (this_l->node.config.logging.log_ipc ())
			{
				BOOST_LOG (this_l->node.log) << boost::str (boost::format ("IPC/RPC request %1% completed in: %2% %3%") % request_id_l % this_l->session_timer.stop ().count () % this_l->session_timer.unit ());
			}
		});

		node.stats.inc (nano::stat::type::ipc, nano::stat::detail::invocations);
		auto body (std::string (reinterpret_cast<char *> (buffer.data ()), buffer.size ()));

		// Note that if the rpc action is async, the shared_ptr<rpc_handler> lifetime will be extended by the action handler
		auto handler (std::make_shared<nano::rpc_handler> (node, server.rpc, body, request_id_l, response_handler_l));
		handler->process_request ();
	}

	/** Async request reader */
	void read_next_request ()
	{
		auto this_l = this->shared_from_this ();

		// Await next request indefinitely
		buffer.resize (sizeof (buffer_size));
		async_read_exactly (buffer.data (), buffer.size (), std::chrono::seconds::max (), [this_l]() {
			if (this_l->buffer[preamble_offset::lead] != 'N' || this_l->buffer[preamble_offset::reserved_1] != 0 || this_l->buffer[preamble_offset::reserved_2] != 0)
			{
				if (this_l->node.config.logging.log_ipc ())
				{
					BOOST_LOG (this_l->node.log) << "IPC: Invalid preamble";
				}
			}
			else if (this_l->buffer[preamble_offset::encoding] == static_cast<uint8_t> (nano::ipc::payload_encoding::json_legacy))
			{
				// Length of payload
				this_l->async_read_exactly (&this_l->buffer_size, sizeof (this_l->buffer_size), [this_l]() {
					boost::endian::big_to_native_inplace (this_l->buffer_size);
					this_l->buffer.resize (this_l->buffer_size);
					// Payload (ptree compliant JSON string)
					this_l->async_read_exactly (this_l->buffer.data (), this_l->buffer_size, [this_l]() {
						this_l->rpc_handle_query ();
					});
				});
			}
			else if (this_l->node.config.logging.log_ipc ())
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

private:
	nano::ipc::ipc_server & server;
	nano::node & node;

	/** Unique session id used for logging */
	uint64_t session_id;

	/** Timer for measuring the duration of ipc calls */
	nano::timer<std::chrono::microseconds> session_timer;

	/**
	 * IO context from node, or per-transport, depending on configuration.
	 * Certain transports may scale better if they use a separate context.
	 */
	boost::asio::io_context & io_ctx;

	/** A socket of the given asio type */
	SOCKET_TYPE socket;

	/** Buffer sizes are read into this */
	uint32_t buffer_size{ 0 };

	/** RPC response */
	std::string response_body;

	/** Buffer used to store data received from the client */
	std::vector<uint8_t> buffer;

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

		// Start serving IO requests. If concurrency_a is < 1, the node's thread pool/io_context is used instead.
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
				BOOST_LOG (server.node.log) << "IPC: acceptor error: " << ec.message ();
			}

			if (acceptor->is_open () && ec != boost::asio::error::operation_aborted)
			{
				this->accept ();
			}
			else
			{
				BOOST_LOG (server.node.log) << "IPC: shutting down";
			}
		});
	}

	void stop ()
	{
		acceptor->close ();
		if (io_ctx)
		{
			io_ctx->stop ();
		}

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

/** The domain socket file is attempted removed at both startup and shutdown. */
class nano::ipc::dsock_file_remover final
{
public:
	dsock_file_remover (std::string const & file_a) :
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
node (node_a), rpc (rpc_a)
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
			transports.push_back (std::make_shared<socket_transport<boost::asio::ip::tcp::acceptor, boost::asio::ip::tcp::socket, boost::asio::ip::tcp::endpoint>> (*this, boost::asio::ip::tcp::endpoint (boost::asio::ip::tcp::v6 (), node_a.config.ipc_config.transport_tcp.port), node_a.config.ipc_config.transport_tcp, threads));
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
}

/** Socket agnostic IO interface */
class channel
{
public:
	virtual void async_read (std::shared_ptr<std::vector<uint8_t>> buffer_a, size_t size_a, std::function<void(boost::system::error_code const &, size_t)> callback_a) = 0;
	virtual void async_write (std::shared_ptr<std::vector<uint8_t>> buffer_a, std::function<void(boost::system::error_code const &, size_t)> callback_a) = 0;
};

/** Domain and TCP client socket */
template <typename SOCKET_TYPE, typename ENDPOINT_TYPE>
class socket_client : public socket_base, public channel
{
public:
	socket_client (boost::asio::io_context & io_ctx_a, ENDPOINT_TYPE endpoint_a) :
	socket_base (io_ctx_a), endpoint (endpoint_a), socket (io_ctx_a), resolver (io_ctx_a)
	{
	}

	void async_resolve (std::string const & host_a, uint16_t port_a, std::function<void(boost::system::error_code const &, boost::asio::ip::tcp::endpoint)> callback_a)
	{
		this->timer_start (io_timeout);
		resolver.async_resolve (boost::asio::ip::tcp::resolver::query (host_a, std::to_string (port_a)), [this, callback_a](boost::system::error_code const & ec, boost::asio::ip::tcp::resolver::iterator endpoint_iterator_a) {
			this->timer_cancel ();
			boost::asio::ip::tcp::resolver::iterator end;
			if (!ec && endpoint_iterator_a != end)
			{
				endpoint = *endpoint_iterator_a;
				callback_a (ec, *endpoint_iterator_a);
			}
			else
			{
				callback_a (ec, *end);
			}
		});
	}

	void async_connect (std::function<void(boost::system::error_code const &)> callback_a)
	{
		this->timer_start (io_timeout);
		socket.async_connect (endpoint, [this, callback_a](boost::system::error_code const & ec) {
			this->timer_cancel ();
			callback_a (ec);
		});
	}

	void async_read (std::shared_ptr<std::vector<uint8_t>> buffer_a, size_t size_a, std::function<void(boost::system::error_code const &, size_t)> callback_a) override
	{
		this->timer_start (io_timeout);
		buffer_a->resize (size_a);
		boost::asio::async_read (socket, boost::asio::buffer (buffer_a->data (), size_a), [this, callback_a](boost::system::error_code const & ec, size_t size_a) {
			this->timer_cancel ();
			callback_a (ec, size_a);
		});
	}

	void async_write (std::shared_ptr<std::vector<uint8_t>> buffer_a, std::function<void(boost::system::error_code const &, size_t)> callback_a) override
	{
		this->timer_start (io_timeout);
		boost::asio::async_write (socket, boost::asio::buffer (buffer_a->data (), buffer_a->size ()), [this, callback_a, buffer_a](boost::system::error_code const & ec, size_t size_a) {
			this->timer_cancel ();
			callback_a (ec, size_a);
		});
	}

	/** Shut down and close socket */
	void close () override
	{
		socket.shutdown (boost::asio::ip::tcp::socket::shutdown_both);
		socket.close ();
	}

private:
	ENDPOINT_TYPE endpoint;
	SOCKET_TYPE socket;
	boost::asio::ip::tcp::resolver resolver;
	std::chrono::seconds io_timeout{ 60 };
};

/**
 * PIMPL class for ipc_client. This ensures that socket_client and boost details can
 * stay out of the header file.
 */
class client_impl : public nano::ipc::ipc_client_impl
{
public:
	client_impl (boost::asio::io_context & io_ctx_a) :
	io_ctx (io_ctx_a)
	{
	}

	void connect (std::string const & host_a, uint16_t port_a, std::function<void(nano::error)> callback_a)
	{
		tcp_client = std::make_shared<socket_client<boost::asio::ip::tcp::socket, boost::asio::ip::tcp::endpoint>> (io_ctx, boost::asio::ip::tcp::endpoint (boost::asio::ip::tcp::v6 (), port_a));

		tcp_client->async_resolve (host_a, port_a, [this, callback_a](boost::system::error_code const & ec_resolve_a, boost::asio::ip::tcp::endpoint endpoint_a) {
			if (!ec_resolve_a)
			{
				this->tcp_client->async_connect ([callback_a](const boost::system::error_code & ec_connect_a) {
					callback_a (nano::error (ec_connect_a));
				});
			}
			else
			{
				callback_a (nano::error (ec_resolve_a));
			}
		});
	}

	nano::error connect (std::string const & path_a)
	{
		nano::error err;
#if defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)
		domain_client = std::make_shared<socket_client<boost::asio::local::stream_protocol::socket, boost::asio::local::stream_protocol::endpoint>> (io_ctx, boost::asio::local::stream_protocol::endpoint (path_a));
#else
		err = nano::error ("Domain sockets are not supported by this platform");
#endif
		return err;
	}

	channel & get_channel ()
	{
#if defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)
		return tcp_client ? static_cast<channel &> (*tcp_client) : static_cast<channel &> (*domain_client);
#else
		return static_cast<channel &> (*tcp_client);
#endif
	}

private:
	boost::asio::io_context & io_ctx;
	std::shared_ptr<socket_client<boost::asio::ip::tcp::socket, boost::asio::ip::tcp::endpoint>> tcp_client;
#if defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)
	std::shared_ptr<socket_client<boost::asio::local::stream_protocol::socket, boost::asio::local::stream_protocol::endpoint>> domain_client;
#endif
};

nano::ipc::ipc_client::ipc_client (boost::asio::io_context & io_ctx_a) :
io_ctx (io_ctx_a)
{
}

nano::error nano::ipc::ipc_client::connect (std::string const & path_a)
{
	impl = std::make_unique<client_impl> (io_ctx);
	return boost::polymorphic_downcast<client_impl *> (impl.get ())->connect (path_a);
}

void nano::ipc::ipc_client::async_connect (std::string const & host_a, uint16_t port_a, std::function<void(nano::error)> callback_a)
{
	impl = std::make_unique<client_impl> (io_ctx);
	auto client (boost::polymorphic_downcast<client_impl *> (impl.get ()));
	client->connect (host_a, port_a, callback_a);
}

nano::error nano::ipc::ipc_client::connect (std::string const & host, uint16_t port)
{
	std::promise<nano::error> result_l;
	async_connect (host, port, [&result_l](nano::error err_a) {
		result_l.set_value (err_a);
	});
	return result_l.get_future ().get ();
}

void nano::ipc::ipc_client::async_write (std::shared_ptr<std::vector<uint8_t>> buffer_a, std::function<void(nano::error, size_t)> callback_a)
{
	auto client (boost::polymorphic_downcast<client_impl *> (impl.get ()));
	client->get_channel ().async_write (buffer_a, [callback_a](const boost::system::error_code & ec_a, size_t bytes_written_a) {
		callback_a (nano::error (ec_a), bytes_written_a);
	});
}

void nano::ipc::ipc_client::async_read (std::shared_ptr<std::vector<uint8_t>> buffer_a, size_t size_a, std::function<void(nano::error, size_t)> callback_a)
{
	auto client (boost::polymorphic_downcast<client_impl *> (impl.get ()));
	client->get_channel ().async_read (buffer_a, size_a, [callback_a](const boost::system::error_code & ec_a, size_t bytes_read_a) {
		callback_a (nano::error (ec_a), bytes_read_a);
	});
}

std::shared_ptr<std::vector<uint8_t>> nano::ipc::ipc_client::prepare_request (nano::ipc::payload_encoding encoding_a, std::string const & payload_a)
{
	auto buffer_l (std::make_shared<std::vector<uint8_t>> ());
	if (encoding_a == nano::ipc::payload_encoding::json_legacy)
	{
		buffer_l->push_back ('N');
		buffer_l->push_back (static_cast<uint8_t> (encoding_a));
		buffer_l->push_back (0);
		buffer_l->push_back (0);

		auto payload_length = static_cast<uint32_t> (payload_a.size ());
		uint32_t be = boost::endian::native_to_big (payload_length);
		char * chars = reinterpret_cast<char *> (&be);
		buffer_l->insert (buffer_l->end (), chars, chars + sizeof (uint32_t));
		buffer_l->insert (buffer_l->end (), payload_a.begin (), payload_a.end ());
	}
	return buffer_l;
}

std::string nano::ipc::rpc_ipc_client::request (std::string const & rpc_action_a)
{
	auto req (prepare_request (nano::ipc::payload_encoding::json_legacy, rpc_action_a));
	auto res (std::make_shared<std::vector<uint8_t>> ());

	std::promise<std::string> result_l;
	async_write (req, [this, &res, &result_l](nano::error err_a, size_t size_a) {
		// Read length
		this->async_read (res, sizeof (uint32_t), [this, &res, &result_l](nano::error err_read_a, size_t size_read_a) {
			uint32_t payload_size_l = boost::endian::big_to_native (*reinterpret_cast<uint32_t *> (res->data ()));
			// Read json payload
			this->async_read (res, payload_size_l, [&res, &result_l](nano::error err_read_a, size_t size_read_a) {
				result_l.set_value (std::string (res->begin (), res->end ()));
			});
		});
	});

	return result_l.get_future ().get ();
}
