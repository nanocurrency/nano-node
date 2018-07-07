#include <algorithm>
#include <api-c/core.pb.h>
#include <boost/array.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/thread/thread_time.hpp>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <rai/node/common.hpp>
#include <rai/node/ipc.hpp>
#include <rai/node/node.hpp>
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
}

bool nano::ipc::ipc_config::deserialize_json (boost::property_tree::ptree & tree_a)
{
	bool error = false;

	auto tcp_l (tree_a.get_child_optional ("tcp"));
	if (tcp_l)
	{
		transport_tcp.io_threads = tcp_l->get<size_t> ("io_threads", std::max (4u, std::thread::hardware_concurrency ()));
		transport_tcp.enabled = tcp_l->get<bool> ("enable", transport_tcp.enabled);
		transport_tcp.control_enabled = tcp_l->get<bool> ("enable_control", transport_tcp.control_enabled);
		transport_tcp.address = tcp_l->get<std::string> ("address", transport_tcp.address);
		transport_tcp.port = tcp_l->get<uint16_t> ("port", transport_tcp.port);
		transport_tcp.io_timeout = tcp_l->get<size_t> ("io_timeout", transport_tcp.io_timeout);
	}

	auto domain_l (tree_a.get_child_optional ("local"));
	if (domain_l)
	{
		transport_domain.io_threads = domain_l->get<size_t> ("io_threads", std::max (4u, std::thread::hardware_concurrency ()));
		transport_domain.enabled = domain_l->get<bool> ("enable", transport_domain.enabled);
		transport_domain.control_enabled = domain_l->get<bool> ("enable_control", transport_domain.control_enabled);
		transport_domain.path = domain_l->get<std::string> ("path", transport_domain.path);
		transport_domain.io_timeout = domain_l->get<size_t> ("io_timeout", transport_domain.io_timeout);
	}

	return error;
}

/** A client session that manages its own lifetime */
template <typename SOCKET_TYPE>
class session : public std::enable_shared_from_this<session<SOCKET_TYPE>>
{
public:
	session (rai::node & node_a, nano::api::api_handler & handler_a, boost::asio::io_context & io_context_a, nano::ipc::ipc_config_transport & config_transport_a) :
	node (node_a), handler (handler_a), io_context (io_context_a), socket (io_context_a), writer_strand (io_context_a), io_timer (io_context_a), config_transport (config_transport_a)
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

	void respond (nano::api::RequestType type, google::protobuf::Message & res)
	{
		nano::api::response response_header;
		response_header.set_type (type);
		response_header.set_error_code (0);
		std::string str_response_header;
		response_header.SerializeToString (&str_response_header);

		std::string response;
		res.SerializeToString (&response);

		uint32_t size_header = boost::endian::native_to_big ((uint32_t)str_response_header.size ());
		uint32_t size_response = boost::endian::native_to_big ((uint32_t)response.size ());
		std::vector<boost::asio::mutable_buffer> bufs = {
			boost::asio::buffer (preamble, 4),
			boost::asio::buffer (&size_header, 4),
			boost::asio::buffer (str_response_header),
			boost::asio::buffer (&size_response, 4),
			boost::asio::buffer (response)
		};

		timer_start (config_transport.io_timeout);
		boost::asio::async_write (socket, bufs,
		writer_strand.wrap (
		boost::bind (
		&session::handle_write,
		this->shared_from_this (),
		boost::asio::placeholders::error,
		boost::asio::placeholders::bytes_transferred)));
	}

	void respond_error (nano::api::RequestType type, std::error_code ec, bool close = false)
	{
		nano::api::response response_header;
		response_header.set_type (type);
		response_header.set_error_code (ec.value ());
		response_header.set_error_message (ec.message ());
		response_header.set_error_category (ec.category ().name ());
		std::string str_response_header;
		response_header.SerializeToString (&str_response_header);

		uint32_t size_header = boost::endian::native_to_big ((uint32_t)str_response_header.size ());

		std::vector<boost::asio::mutable_buffer> bufs = {
			boost::asio::buffer (preamble, 4),
			boost::asio::buffer (&size_header, 4),
			boost::asio::buffer (str_response_header)
		};

		timer_start (config_transport.io_timeout);
		boost::asio::async_write (socket, bufs,
		writer_strand.wrap (
		boost::bind (
		&session::handle_write,
		this->shared_from_this (),
		boost::asio::placeholders::error,
		boost::asio::placeholders::bytes_transferred)));
	}

	/** Write callback. If no error occurs, the session starts waiting for another request. */
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

	/** Hand the raw protobuffer over to the API handler which parses and executes the query */
	void handle_query ()
	{
		node.stats.inc (rai::stat::type::api, rai::stat::detail::invocations);
		auto res = handler.parse (request_header.type (), buffer);
		if (res)
		{
			respond (request_header.type (), *res.value ());
		}
		else
		{
			respond_error (request_header.type (), res.error ());
		}
	}

	/** Async request reader */
	void read_next_request ()
	{
		auto this_l = this->shared_from_this ();

		// Await preamble
		buffer.resize (4);
		async_read_exactly (buffer.data (), buffer.size (), std::numeric_limits<size_t>::max (), [this_l]() {
			if (this_l->buffer[0] != 'N' || this_l->buffer[1] != 0x0)
			{
				BOOST_LOG (this_l->node.log) << "IPC: Invalid preamble";
				this_l->respond_error (nano::api::RequestType::INVALID, nano::error_ipc::invalid_preamble, true);
			}
			else
			{
				// Size of query header
				this_l->async_read_exactly (&this_l->buffer_size, 4, [this_l]() {
					boost::endian::big_to_native_inplace (this_l->buffer_size);
					this_l->buffer.resize (this_l->buffer_size);
					// Query header
					this_l->async_read_exactly (this_l->buffer.data (), this_l->buffer_size, [this_l]() {
						if (this_l->request_header.ParseFromArray (this_l->buffer.data (), this_l->buffer.size ()))
						{
							// Length of query
							this_l->async_read_exactly (&this_l->buffer_size, 4, [this_l]() {
								boost::endian::big_to_native_inplace (this_l->buffer_size);
								this_l->buffer.resize (this_l->buffer_size);
								// Query
								this_l->async_read_exactly (this_l->buffer.data (), this_l->buffer_size, [this_l]() {
									this_l->handle_query ();
								});
							});
						}
						else
						{
							BOOST_LOG (this_l->node.log) << "IPC: Could not parse query header";
						}
					});
				});
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
	rai::node & node;
	nano::api::api_handler & handler;
	nano::api::request request_header;

	/**
	 * IO context from node, or per-transport, depending on configuration.
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

	/** Preamble is 'N', encoding, major, minor */
	uint8_t preamble[4]{ 'N', 0, nano::api::VERSION_MAJOR, nano::api::VERSION_MINOR };
};

/** Domain and TCP socket transport */
template <typename ACCEPTOR_TYPE, typename SOCKET_TYPE, typename ENDPOINT_TYPE>
class socket_transport : public nano::ipc::transport
{
public:
	socket_transport (rai::node & node_a, nano::api::api_handler & handler_a, ENDPOINT_TYPE ep, nano::ipc::ipc_config_transport & config_transport_a, int concurrency) :
	node (node_a), config_transport (config_transport_a), handler (handler_a)
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
			runner = std::make_unique<rai::thread_runner> (*io_context, concurrency);
		}
	}

	boost::asio::io_context & context () const
	{
		return io_context ? *io_context : node.service;
	}

	void accept ()
	{
		std::shared_ptr<session<SOCKET_TYPE>> new_session (new session<SOCKET_TYPE> (node, handler, io_context ? *io_context : node.service, config_transport));

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

		accept ();
	}

	void stop ()
	{
		io_context->stop ();

		if (runner)
		{
			runner->join ();
		}
	}

private:
	rai::node & node;
	nano::ipc::ipc_config_transport & config_transport;
	nano::api::api_handler & handler;
	std::unique_ptr<rai::thread_runner> runner;
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

nano::ipc::ipc_server::ipc_server (rai::node & node_a) :
node (node_a),
handler (node_a),
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
			transports.push_back (std::make_shared<socket_transport<boost::asio::local::stream_protocol::acceptor, boost::asio::local::stream_protocol::socket, boost::asio::local::stream_protocol::endpoint>> (node_a, handler, ep, node_a.config.ipc_config.transport_domain, threads));
#else
			BOOST_LOG (node.log) << "IPC: Domain sockets are not supported on this platform";
#endif
		}

		if (node_a.config.ipc_config.transport_tcp.enabled)
		{
			size_t threads = node_a.config.ipc_config.transport_tcp.io_threads;
			transports.push_back (std::make_shared<socket_transport<boost::asio::ip::tcp::acceptor, boost::asio::ip::tcp::socket, boost::asio::ip::tcp::endpoint>> (node_a, handler, boost::asio::ip::tcp::endpoint (boost::asio::ip::tcp::v6 (), node_a.config.ipc_config.transport_tcp.port), node_a.config.ipc_config.transport_domain, threads));
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
