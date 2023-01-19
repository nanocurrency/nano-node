#include <nano/boost/asio/bind_executor.hpp>
#include <nano/boost/asio/ip/tcp.hpp>
#include <nano/boost/asio/local/stream_protocol.hpp>
#include <nano/boost/asio/read.hpp>
#include <nano/boost/asio/strand.hpp>
#include <nano/lib/asio.hpp>
#include <nano/lib/ipc.hpp>
#include <nano/lib/ipc_client.hpp>

#include <boost/endian/conversion.hpp>
#include <boost/polymorphic_cast.hpp>

#include <deque>
#include <future>

namespace
{
/** Socket agnostic IO interface */
class channel
{
public:
	virtual void async_read (std::shared_ptr<std::vector<uint8_t>> const & buffer_a, size_t size_a, std::function<void (boost::system::error_code const &, size_t)> callback_a) = 0;
	virtual void async_write (nano::shared_const_buffer const & buffer_a, std::function<void (boost::system::error_code const &, size_t)> callback_a) = 0;

	/**
	 * Read a length-prefixed message asynchronously using the given timeout. This is suitable for full duplex scenarios where it may
	 * take an arbitrarily long time for the node to send messages for a given subscription.
	 * Received length must be a big endian 32-bit unsigned integer.
	 * @param buffer_a Receives the payload
	 * @param timeout_a How long to await message data. In some scenarios, such as waiting for data on subscriptions, specifying std::chrono::seconds::max() makes sense.
	 * @param callback_a If called without errors, the payload buffer is successfully populated
	 */
	virtual void async_read_message (std::shared_ptr<std::vector<uint8_t>> const & buffer_a, std::chrono::seconds timeout_a, std::function<void (boost::system::error_code const &, size_t)> callback_a) = 0;
};

/* Boost v1.70 introduced breaking changes; the conditional compilation allows 1.6x to be supported as well. */
#if BOOST_VERSION < 107000
using socket_type = boost::asio::ip::tcp::socket;
#else
using socket_type = boost::asio::basic_stream_socket<boost::asio::ip::tcp, boost::asio::io_context::executor_type>;
#endif

/** Domain and TCP client socket */
template <typename SOCKET_TYPE, typename ENDPOINT_TYPE>
class socket_client : public nano::ipc::socket_base, public channel, public std::enable_shared_from_this<socket_client<SOCKET_TYPE, ENDPOINT_TYPE>>
{
public:
	socket_client (boost::asio::io_context & io_ctx_a, ENDPOINT_TYPE endpoint_a) :
		socket_base (io_ctx_a), endpoint (std::move (endpoint_a)), socket (io_ctx_a), resolver (io_ctx_a), strand (io_ctx_a.get_executor ())
	{
	}

	void async_resolve (std::string const & host_a, uint16_t port_a, std::function<void (boost::system::error_code const &, boost::asio::ip::tcp::endpoint)> callback_a)
	{
		auto this_l (this->shared_from_this ());
		this_l->timer_start (io_timeout);
		resolver.async_resolve (boost::asio::ip::tcp::resolver::query (host_a, std::to_string (port_a)), [this_l, callback = std::move (callback_a)] (boost::system::error_code const & ec, boost::asio::ip::tcp::resolver::iterator endpoint_iterator_a) {
			this_l->timer_cancel ();
			boost::asio::ip::tcp::resolver::iterator end;
			if (!ec && endpoint_iterator_a != end)
			{
				this_l->endpoint = *endpoint_iterator_a;
				callback (ec, *endpoint_iterator_a);
			}
			else
			{
				callback (ec, boost::asio::ip::tcp::endpoint ());
			}
		});
	}

	void async_connect (std::function<void (boost::system::error_code const &)> callback_a)
	{
		auto this_l (this->shared_from_this ());
		this_l->timer_start (io_timeout);
		socket.async_connect (endpoint, boost::asio::bind_executor (strand, [this_l, callback = std::move (callback_a)] (boost::system::error_code const & ec) {
			this_l->timer_cancel ();
			callback (ec);
		}));
	}

	void async_read (std::shared_ptr<std::vector<uint8_t>> const & buffer_a, size_t size_a, std::function<void (boost::system::error_code const &, size_t)> callback_a) override
	{
		auto this_l (this->shared_from_this ());
		this_l->timer_start (io_timeout);
		buffer_a->resize (size_a);
		boost::asio::async_read (socket, boost::asio::buffer (buffer_a->data (), size_a), boost::asio::bind_executor (this_l->strand, [this_l, buffer_a, callback_a] (boost::system::error_code const & ec, size_t size_a) {
			this_l->timer_cancel ();
			callback_a (ec, size_a);
		}));
	}

	void async_write (nano::shared_const_buffer const & buffer_a, std::function<void (boost::system::error_code const &, size_t)> callback_a) override
	{
		auto this_l (this->shared_from_this ());
		boost::asio::post (strand, boost::asio::bind_executor (strand, [buffer_a, callback_a, this_l] () {
			bool write_in_progress = !this_l->send_queue.empty ();
			auto queue_size = this_l->send_queue.size ();
			if (queue_size < this_l->queue_size_max)
			{
				this_l->send_queue.emplace_back (buffer_a, callback_a);
			}
			if (!write_in_progress)
			{
				this_l->write_queued_messages ();
			}
		}));
	}

	// TODO: investigate clang-tidy warning about recursive call chain
	//
	void write_queued_messages ()
	{
		auto this_l (this->shared_from_this ());
		auto msg (send_queue.front ());
		this_l->timer_start (io_timeout);
		nano::async_write (socket, msg.buffer,
		boost::asio::bind_executor (strand,
		[msg, this_l] (boost::system::error_code ec, std::size_t size_a) {
			this_l->timer_cancel ();

			if (msg.callback)
			{
				msg.callback (ec, size_a);
			}

			this_l->send_queue.pop_front ();
			if (!ec && !this_l->send_queue.empty ())
			{
				this_l->write_queued_messages ();
			}
		}));
	}

	void async_read_message (std::shared_ptr<std::vector<uint8_t>> const & buffer_a, std::chrono::seconds timeout_a, std::function<void (boost::system::error_code const &, size_t)> callback_a) override
	{
		auto this_l (this->shared_from_this ());
		this_l->timer_start (timeout_a);
		buffer_a->resize (4);
		// Read 32 bit big-endian length
		boost::asio::async_read (socket, boost::asio::buffer (buffer_a->data (), 4), boost::asio::bind_executor (this_l->strand, [this_l, timeout_a, buffer_a, callback_a] (boost::system::error_code const & ec, size_t size_a) {
			this_l->timer_cancel ();
			if (!ec)
			{
				uint32_t payload_size_l = boost::endian::big_to_native (*reinterpret_cast<uint32_t *> (buffer_a->data ()));
				buffer_a->resize (payload_size_l);
				// Read payload
				this_l->timer_start (timeout_a);
				this_l->async_read (buffer_a, payload_size_l, [this_l, buffer_a, callback_a] (boost::system::error_code const & ec_a, size_t size_a) {
					this_l->timer_cancel ();
					callback_a (ec_a, size_a);
				});
			}
			else
			{
				callback_a (ec, size_a);
			}
		}));
	}

	/** Shut down and close socket */
	void close () override
	{
		auto this_l (this->shared_from_this ());
		boost::asio::post (strand, boost::asio::bind_executor (strand, [this_l] () {
			this_l->socket.shutdown (boost::asio::ip::tcp::socket::shutdown_both);
			this_l->socket.close ();
		}));
	}

private:
	/** Holds the buffer and callback for queued writes */
	class queue_item
	{
	public:
		queue_item (nano::shared_const_buffer buffer_a, std::function<void (boost::system::error_code const &, size_t)> callback_a) :
			buffer (std::move (buffer_a)), callback (std::move (callback_a))
		{
		}
		nano::shared_const_buffer buffer;
		std::function<void (boost::system::error_code const &, size_t)> callback;
	};
	size_t const queue_size_max = 64 * 1024;
	/** The send queue is protected by always being accessed through the strand */
	std::deque<queue_item> send_queue;

	ENDPOINT_TYPE endpoint;
	SOCKET_TYPE socket;
	boost::asio::ip::tcp::resolver resolver;
	std::chrono::seconds io_timeout{ 60 };
	boost::asio::strand<boost::asio::io_context::executor_type> strand;
};

/**
 * PIMPL class for ipc_client. This ensures that socket_client and boost details can
 * stay out of the header file.
 */
class client_impl : public nano::ipc::ipc_client_impl
{
public:
	explicit client_impl (boost::asio::io_context & io_ctx_a) :
		io_ctx (io_ctx_a)
	{
	}

	void connect (std::string const & host_a, uint16_t port_a, std::function<void (nano::error)> callback_a)
	{
		tcp_client = std::make_shared<socket_client<socket_type, boost::asio::ip::tcp::endpoint>> (io_ctx, boost::asio::ip::tcp::endpoint (boost::asio::ip::tcp::v6 (), port_a));

		tcp_client->async_resolve (host_a, port_a, [this, callback = std::move (callback_a)] (boost::system::error_code const & ec_resolve_a, boost::asio::ip::tcp::endpoint const &) mutable {
			if (!ec_resolve_a)
			{
				this->tcp_client->async_connect ([cbk = std::move (callback)] (boost::system::error_code const & ec_connect_a) {
					cbk (nano::error (ec_connect_a));
				});
			}
			else
			{
				callback (nano::error (ec_resolve_a));
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
	std::shared_ptr<socket_client<socket_type, boost::asio::ip::tcp::endpoint>> tcp_client;
#if defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)
	std::shared_ptr<socket_client<boost::asio::local::stream_protocol::socket, boost::asio::local::stream_protocol::endpoint>> domain_client;
#endif
};
}

nano::ipc::ipc_client::ipc_client (boost::asio::io_context & io_ctx_a) :
	io_ctx (io_ctx_a)
{
}

nano::error nano::ipc::ipc_client::connect (std::string const & path_a)
{
	impl = std::make_unique<client_impl> (io_ctx);
	return boost::polymorphic_downcast<client_impl *> (impl.get ())->connect (path_a);
}

void nano::ipc::ipc_client::async_connect (std::string const & host_a, uint16_t port_a, std::function<void (nano::error)> callback_a)
{
	impl = std::make_unique<client_impl> (io_ctx);
	auto client (boost::polymorphic_downcast<client_impl *> (impl.get ()));
	client->connect (host_a, port_a, std::move (callback_a));
}

nano::error nano::ipc::ipc_client::connect (std::string const & host, uint16_t port)
{
	std::promise<nano::error> result_l;
	async_connect (host, port, [&result_l] (nano::error err_a) {
		result_l.set_value (std::move (err_a));
	});
	return result_l.get_future ().get ();
}

void nano::ipc::ipc_client::async_write (nano::shared_const_buffer const & buffer_a, std::function<void (nano::error, size_t)> callback_a)
{
	auto client (boost::polymorphic_downcast<client_impl *> (impl.get ()));
	client->get_channel ().async_write (buffer_a, [callback = std::move (callback_a)] (boost::system::error_code const & ec_a, size_t bytes_written_a) {
		callback (nano::error (ec_a), bytes_written_a);
	});
}

void nano::ipc::ipc_client::async_read (std::shared_ptr<std::vector<uint8_t>> const & buffer_a, size_t size_a, std::function<void (nano::error, size_t)> callback_a)
{
	auto client (boost::polymorphic_downcast<client_impl *> (impl.get ()));
	client->get_channel ().async_read (buffer_a, size_a, [callback = std::move (callback_a), buffer_a] (boost::system::error_code const & ec_a, size_t bytes_read_a) {
		callback (nano::error (ec_a), bytes_read_a);
	});
}

/** Read a length-prefixed message asynchronously. Received length must be a big endian 32-bit unsigned integer. */
void nano::ipc::ipc_client::async_read_message (std::shared_ptr<std::vector<uint8_t>> const & buffer_a, std::chrono::seconds timeout_a, std::function<void (nano::error, size_t)> callback_a)
{
	auto client (boost::polymorphic_downcast<client_impl *> (impl.get ()));
	client->get_channel ().async_read_message (buffer_a, timeout_a, [callback = std::move (callback_a), buffer_a] (boost::system::error_code const & ec_a, size_t bytes_read_a) {
		callback (nano::error (ec_a), bytes_read_a);
	});
}

std::vector<uint8_t> nano::ipc::get_preamble (nano::ipc::payload_encoding encoding_a)
{
	std::vector<uint8_t> buffer_l;
	buffer_l.push_back ('N');
	buffer_l.push_back (static_cast<uint8_t> (encoding_a));
	buffer_l.push_back (0);
	buffer_l.push_back (0);
	return buffer_l;
}

nano::shared_const_buffer nano::ipc::prepare_flatbuffers_request (std::shared_ptr<flatbuffers::FlatBufferBuilder> const & flatbuffer_a)
{
	auto buffer_l (get_preamble (nano::ipc::payload_encoding::flatbuffers));
	auto payload_length = static_cast<uint32_t> (flatbuffer_a->GetSize ());
	uint32_t be = boost::endian::native_to_big (payload_length);
	char * chars = reinterpret_cast<char *> (&be);
	buffer_l.insert (buffer_l.end (), chars, chars + sizeof (uint32_t));
	buffer_l.insert (buffer_l.end (), flatbuffer_a->GetBufferPointer (), flatbuffer_a->GetBufferPointer () + flatbuffer_a->GetSize ());
	return nano::shared_const_buffer{ std::move (buffer_l) };
}

nano::shared_const_buffer nano::ipc::prepare_request (nano::ipc::payload_encoding encoding_a, std::string const & payload_a)
{
	std::vector<uint8_t> buffer_l;
	if (encoding_a == nano::ipc::payload_encoding::json_v1 || encoding_a == nano::ipc::payload_encoding::flatbuffers_json)
	{
		buffer_l = get_preamble (encoding_a);
		auto payload_length = static_cast<uint32_t> (payload_a.size ());
		uint32_t be = boost::endian::native_to_big (payload_length);
		char * chars = reinterpret_cast<char *> (&be);
		buffer_l.insert (buffer_l.end (), chars, chars + sizeof (uint32_t));
		buffer_l.insert (buffer_l.end (), payload_a.begin (), payload_a.end ());
	}
	return nano::shared_const_buffer{ std::move (buffer_l) };
}

std::string nano::ipc::request (nano::ipc::payload_encoding encoding_a, nano::ipc::ipc_client & ipc_client, std::string const & rpc_action_a)
{
	auto req (prepare_request (encoding_a, rpc_action_a));
	auto res (std::make_shared<std::vector<uint8_t>> ());

	std::promise<std::string> result_l;
	ipc_client.async_write (req, [&ipc_client, &res, &result_l] (nano::error const &, size_t size_a) {
		// Read length
		ipc_client.async_read (res, sizeof (uint32_t), [&ipc_client, &res, &result_l] (nano::error const &, size_t size_read_a) {
			uint32_t payload_size_l = boost::endian::big_to_native (*reinterpret_cast<uint32_t *> (res->data ()));
			// Read json payload
			ipc_client.async_read (res, payload_size_l, [&res, &result_l] (nano::error const &, size_t size_read_a) {
				result_l.set_value (std::string (res->begin (), res->end ()));
			});
		});
	});

	return result_l.get_future ().get ();
}
