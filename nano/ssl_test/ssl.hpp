#pragma once

#include <nano/lib/utility.hpp>
#include <nano/node/ssl/ssl_classes.hpp>
#include <nano/node/ssl/ssl_functions.hpp>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/write.hpp>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

constexpr std::string_view CA_PRIVATE_KEY_HEX_1 = "c1e9ad082d069109d8552e547717815e25bb3d682ff86d1d097a0c80e7db9a65";
constexpr std::string_view CA_PUBLIC_KEY_HEX_1 = "25927d85eba160169c9ccc036d974695249a67bd8b93c00e6f842ddab1ad3b77";
constexpr std::string_view CA_PRIVATE_KEY_HEX_2 = "254d14339368027bf7510d45077ac3e67d7b3507be13a4cf3c6cfb5a2b6a5359";
constexpr std::string_view CA_PUBLIC_KEY_HEX_2 = "1b04ed75774b09f1427a664b90b8728ab11e9e9b4bb739c8498d2e1767c5a66e";

namespace
{
void expect (bool condition)
{
	if (!condition)
	{
		std::cout << nano::generate_stacktrace () << std::endl;
		throw std::runtime_error{ "expect: condition failed" };
	}
}

}

namespace nano::test::ssl
{
class io_context
{
public:
	io_context () :
		m_context{},
		m_work{ std::make_unique<boost::asio::io_context::work> (get ()) },
		m_thread{ [this] () {
			get ().run ();
		} }
	{
	}

	~io_context ()
	{
		m_work.reset ();
		m_thread.join ();
	}

	boost::asio::io_context & get ()
	{
		return m_context;
	}

	boost::asio::io_context & operator* ()
	{
		return get ();
	}

private:
	boost::asio::io_context m_context;
	std::unique_ptr<boost::asio::io_context::work> m_work;
	std::thread m_thread;
};

class connection_entity
{
public:
	explicit connection_entity (boost::asio::io_context & io_context) :
		m_io_context{ io_context }
	{
	}

protected:
	boost::asio::io_context & get_io_context ()
	{
		return m_io_context;
	}

private:
	boost::asio::io_context & m_io_context;
};

class ssl_protocol
{
public:
	static constexpr std::size_t handshake_max_size = 512;
	static constexpr std::size_t client_hello_min_size = 6;
	static constexpr std::size_t server_hello_min_size = 6;

	static bool is_client_hello (const nano::ssl::BufferView & data)
	{
		return data.getSize () >= client_hello_min_size && is_hello (data) && data[5] == 0x01;
	}

private:
	static bool is_hello (const nano::ssl::BufferView & data)
	{
		return data.getSize () >= std::min (client_hello_min_size, server_hello_min_size) && data[0] == 0x16 && data[1] == 0x03 && (data[2] == 0x01 || data[2] == 0x03 || data[2] == 0x04);
	}
};

class socket
{
public:
	explicit socket (boost::asio::io_context & io_context) :
		m_socket{ io_context },
		m_is_connected{ false },
		m_errors{},
		m_read_buffer{},
		m_write_buffer{}
	{
	}

	virtual ~socket ()
	{
		close ();
	}

	void close ()
	{
		if (is_connected ())
		{
			boost::system::error_code error_code{};
			m_socket.shutdown (boost::asio::socket_base::shutdown_both, error_code);
			m_socket.close (error_code);

			m_is_connected = false;
			m_errors.clear ();
		}
	}

	boost::asio::ip::tcp::socket & get ()
	{
		return m_socket;
	}

	boost::asio::ip::tcp::socket & operator* ()
	{
		return get ();
	}

	void add_error (const boost::system::error_code & error_code)
	{
		if (error_code != boost::asio::error::operation_aborted)
		{
			std::cout << "socket::add_error -- " << error_code << std::endl;
			m_errors += error_code.message () + ", ";
		}
	}

	const std::string & get_errors () const
	{
		return m_errors;
	}

	virtual void mark_as_connected ()
	{
		m_is_connected = true;
	}

	bool is_connected () const
	{
		return m_is_connected;
	}

	nano::ssl::Buffer & get_read_buffer ()
	{
		if (m_read_buffer.empty ())
		{
			m_read_buffer.resize (read_buffer_size);
		}

		return m_read_buffer;
	}

	void clear_read_buffer ()
	{
		m_read_buffer.clear ();
	}

	nano::ssl::Buffer & get_write_buffer ()
	{
		if (m_write_buffer.empty ())
		{
			m_write_buffer.resize (write_buffer_size);
		}

		return m_write_buffer;
	}

	void clear_write_buffer ()
	{
		m_write_buffer.clear ();
	}

private:
	static constexpr std::size_t read_buffer_size = 1024;
	static constexpr std::size_t write_buffer_size = 1024;

	boost::asio::ip::tcp::socket m_socket;
	std::atomic_bool m_is_connected;
	std::string m_errors;
	nano::ssl::Buffer m_read_buffer;
	nano::ssl::Buffer m_write_buffer;
};

class ssl_socket : public socket
{
public:
	using stream = boost::asio::ssl::stream<boost::asio::ip::tcp::socket &>;

	explicit ssl_socket (boost::asio::io_context & io_context, nano::ssl::ssl_context & ssl_context) :
		socket{ io_context },
		m_stream{ get (), *ssl_context },
		m_ensurer{ std::make_optional<nano::ssl::ssl_manual_validation_ensurer> () },
		m_handshake_state{ handshake_state::NONE }
	{
		nano::ssl::setCaPublicKeyValidator (nano::ssl::SslPtrView::make (m_stream.native_handle ()), m_ensurer->get_handler ());
	}

	stream & get_stream ()
	{
		return m_stream;
	}

	void mark_as_connected () override
	{
		if (m_ensurer.has_value () && !m_ensurer->was_invoked ())
		{
			throw std::runtime_error{ "ssl_custom_pki_validator: not invoked -- this can be a potential MiTM attack" };
		}

		move_to_handshake_none ();
		clear_read_buffer ();
		clear_write_buffer ();

		socket::mark_as_connected ();
	}

	void mark_as_downgrade_connected ()
	{
		m_ensurer.reset ();
		mark_as_connected ();
	}

	void move_to_handshake_none ()
	{
		m_handshake_state = handshake_state::NONE;
	}

	bool is_handshake_none () const
	{
		return handshake_state::NONE == m_handshake_state;
	}

	void move_to_handshake_client_hello ()
	{
		expect (is_handshake_none ());
		m_handshake_state = handshake_state::CLIENT_HELLO;
	}

	bool is_handshake_client_hello () const
	{
		return handshake_state::CLIENT_HELLO == m_handshake_state;
	}

	void move_to_handshake_server_hello ()
	{
		expect (is_handshake_client_hello ());
		m_handshake_state = handshake_state::SERVER_HELLO;
	}

	bool is_handshake_server_hello () const
	{
		return handshake_state::SERVER_HELLO == m_handshake_state;
	}

private:
	enum class handshake_state
	{
		NONE,
		CLIENT_HELLO,
		SERVER_HELLO,
	};

	stream m_stream;
	handshake_state m_handshake_state;
	std::optional<nano::ssl::ssl_manual_validation_ensurer> m_ensurer;
};

class server : public connection_entity, public std::enable_shared_from_this<server>
{
public:
	explicit server (boost::asio::io_context & io_context) :
		connection_entity{ io_context },
		m_acceptor{ io_context },
		m_client_sockets{}
	{
	}

	virtual ~server () = default;

	virtual void close ()
	{
		std::cout << "server::close" << std::endl;

		m_client_sockets.clear ();
		if (m_acceptor.is_open ())
		{
			boost::system::error_code error_code{};
			m_acceptor.close (error_code);
		}
	}

	void run (const std::uint16_t port)
	{
		std::cout << "server::run" << std::endl;

		const boost::asio::ip::tcp::endpoint endpoint{ boost::asio::ip::address_v4::any (), port };
		m_acceptor.open (endpoint.protocol ());
		m_acceptor.set_option (boost::asio::ip::tcp::acceptor::reuse_address (true));
		m_acceptor.bind (endpoint);
		m_acceptor.listen ();

		accept ();
	}

	const std::vector<std::unique_ptr<socket>> & get_client_sockets () const
	{
		return m_client_sockets;
	}

private:
	boost::asio::ip::tcp::acceptor m_acceptor;
	std::vector<std::unique_ptr<socket>> m_client_sockets;

	void accept ()
	{
		std::cout << "server::accept" << std::endl;

		auto client_socket = create_new_client_socket ();
		auto & client_socket_ref = client_socket->get ();
		m_acceptor.async_accept (
		client_socket_ref,
		std::bind (
		&server::on_accept,
		shared_from_this (),
		std::placeholders::_1,
		std::move (client_socket)));
	}

	void on_accept (const boost::system::error_code & error_code, std::unique_ptr<socket> & client_socket)
	{
		std::cout << "server::on_accept" << std::endl;

		if (error_code)
		{
			return client_socket->add_error (error_code);
		}

		auto & client_socket_ref = *client_socket;
		m_client_sockets.push_back (std::move (client_socket));

		on_accept_impl (client_socket_ref);
		accept ();
	}

	virtual void on_accept_impl (socket & client_socket) = 0;

	virtual std::unique_ptr<socket> create_new_client_socket () = 0;
};

class plain_server final : public server
{
public:
	explicit plain_server (boost::asio::io_context & io_context) :
		server{ io_context }
	{
	}

private:
	void on_accept_impl (socket & client_socket) override
	{
		std::cout << "plain_server::on_accept_impl -- marking client socket as connected" << std::endl;
		client_socket.mark_as_connected ();

		// TODO: do this asynchronously
		nano::ssl::Buffer data (ssl_protocol::server_hello_min_size);
		client_socket.get ().write_some (boost::asio::buffer (data));
	}

	std::unique_ptr<socket> create_new_client_socket () override
	{
		return std::make_unique<socket> (get_io_context ());
	}
};

class ssl_server final : public server
{
public:
	explicit ssl_server (boost::asio::io_context & io_context) :
		server{ io_context },
		m_ssl_context{ nano::ssl::key_group{ CA_PRIVATE_KEY_HEX_1, CA_PUBLIC_KEY_HEX_1 }, "test_server_pki" }
	{
	}

private:
	nano::ssl::ssl_context m_ssl_context;

	void on_accept_impl (socket & client_socket) override
	{
		std::cout << "ssl_server::on_accept_impl" << std::endl;

		static_cast<ssl_socket &> (client_socket).move_to_handshake_client_hello ();
		client_socket.get ().async_read_some (
		boost::asio::buffer (
		client_socket.get_read_buffer ().data (),
		ssl_protocol::client_hello_min_size),
		std::bind (
		&ssl_server::on_read,
		std::static_pointer_cast<ssl_server> (shared_from_this ()),
		std::placeholders::_1,
		std::placeholders::_2,
		std::ref (client_socket)));
	}

	void on_read (const boost::system::error_code & error_code, const std::size_t dataSize, socket & client_socket) const
	{
		std::cout << "ssl_server::on_read -- error_code = " << error_code << ", dataSize = " << dataSize << std::endl;

		if (error_code)
		{
			return client_socket.add_error (error_code);
		}

		if (client_socket.is_connected ())
		{
			std::cout << "ssl_server::on_read -- socket already connected, ignoring" << std::endl;
			return client_socket.clear_read_buffer ();
		}

		auto & ssl_client_socket = static_cast<ssl_socket &> (client_socket);
		expect (ssl_client_socket.is_handshake_client_hello () || ssl_client_socket.is_handshake_server_hello ());

		auto & data = ssl_client_socket.get_read_buffer ();
		if (ssl_client_socket.is_handshake_client_hello ())
		{
			if (ssl_protocol::is_client_hello (data))
			{
				std::cout << "ssl_server::on_read -- probably talking to a secure client, trying to fully handshake" << std::endl;
				ssl_client_socket.move_to_handshake_server_hello ();

				ssl_client_socket.get ().async_read_some (
				boost::asio::buffer (
				data.data () + ssl_protocol::client_hello_min_size,
				ssl_protocol::handshake_max_size - ssl_protocol::client_hello_min_size),
				std::bind (
				&ssl_server::on_read,
				std::static_pointer_cast<const ssl_server> (
				shared_from_this ()),
				std::placeholders::_1,
				std::placeholders::_2,
				std::ref (client_socket)));
			}
			else
			{
				std::cout << "ssl_server::on_accept_impl -- probably talking to a plain client"
						  << ", marking client socket as downgrade connected"
						  << std::endl;

				ssl_client_socket.mark_as_downgrade_connected ();
			}
		}
		else
		{
			ssl_client_socket.get_stream ().async_handshake (
			boost::asio::ssl::stream_base::server,
			boost::asio::buffer (
			data.data (),
			ssl_protocol::client_hello_min_size + dataSize),
			std::bind (
			&ssl_server::on_handshake,
			std::static_pointer_cast<const ssl_server> (
			shared_from_this ()),
			std::placeholders::_1,
			std::ref (client_socket)));
		}
	}

	std::unique_ptr<socket> create_new_client_socket () override
	{
		return std::make_unique<ssl_socket> (get_io_context (), m_ssl_context);
	}

	void on_handshake (const boost::system::error_code & error_code, socket & client_socket) const
	{
		std::cout << "ssl_server::on_handshake -- error_code = " << error_code << std::endl;

		if (error_code)
		{
			return client_socket.add_error (error_code);
		}

		std::cout << "ssl_server::on_accept_impl -- finished handshake, marking client socket as connected" << std::endl;
		client_socket.mark_as_connected ();
	}
};

class client : public connection_entity, public std::enable_shared_from_this<client>
{
public:
	explicit client (boost::asio::io_context & io_context) :
		connection_entity{ io_context }
	{
	}

	virtual ~client () = default;

	virtual void close () = 0;

	void run (const std::uint16_t port)
	{
		std::cout << "client::run" << std::endl;

		const boost::asio::ip::tcp::endpoint endpoint{ boost::asio::ip::address_v4::any (), port };
		get_socket ().get ().async_connect (
		endpoint,
		std::bind (
		&client::on_connect,
		shared_from_this (),
		std::placeholders::_1));
	}

	virtual socket & get_socket () = 0;

private:
	void on_connect (const boost::system::error_code & error_code)
	{
		std::cout << "client::on_connect -- error_code = " << error_code << std::endl;

		if (error_code)
		{
			return get_socket ().add_error (error_code);
		}

		on_connect_impl ();
	}

	virtual void on_connect_impl () = 0;
};

class plain_client final : public client
{
public:
	explicit plain_client (boost::asio::io_context & io_context) :
		client{ io_context },
		m_socket{ io_context }
	{
	}

	void close () override
	{
		m_socket.close ();
	}

private:
	socket m_socket;

	void on_connect_impl () override
	{
		std::cout << "plain_client::on_connect_impl -- marking socket as connected" << std::endl;
		m_socket.mark_as_connected ();

		// TODO: do this asynchronously
		nano::ssl::Buffer data (ssl_protocol::client_hello_min_size);
		m_socket.get ().write_some (boost::asio::buffer (data));
	}

	socket & get_socket () override
	{
		return m_socket;
	}
};

class ssl_client final : public client
{
public:
	explicit ssl_client (boost::asio::io_context & io_context) :
		client{ io_context },
		m_try_ssl{ true },
		m_ssl_context{ nano::ssl::key_group{ CA_PRIVATE_KEY_HEX_2, CA_PUBLIC_KEY_HEX_2 }, "test_client_pki" },
		m_socket{},
		m_ssl_socket{}
	{
	}

	void close () override
	{
		std::cout << "ssl_client::close" << std::endl;

		if (m_try_ssl)
		{
			m_ssl_socket->close ();
			m_ssl_socket.reset ();
		}
		else
		{
			m_socket->close ();
			m_socket.reset ();
		}

		m_try_ssl = true;
	}

private:
	bool m_try_ssl;
	nano::ssl::ssl_context m_ssl_context;
	std::unique_ptr<socket> m_socket;
	std::unique_ptr<ssl_socket> m_ssl_socket;

	void on_connect_impl () override
	{
		if (m_try_ssl)
		{
			std::cout << "ssl_client::on_connect_impl -- with SSL -- initiating handshake" << std::endl;
			m_ssl_socket->get_stream ().async_handshake (
			boost::asio::ssl::stream_base::client,
			std::bind (
			&ssl_client::on_handshake,
			std::static_pointer_cast<ssl_client> (
			shared_from_this ()),
			std::placeholders::_1));
		}
		else
		{
			std::cout << "ssl_client::on_connect_impl -- no SSL -- marking socket as connected" << std::endl;
			m_socket->mark_as_connected ();
		}
	}

	socket & get_socket () override
	{
		if (m_try_ssl)
		{
			if (!m_ssl_socket)
			{
				m_ssl_socket = std::make_unique<ssl_socket> (get_io_context (), m_ssl_context);
			}

			return *m_ssl_socket;
		}

		if (!m_socket)
		{
			m_socket = std::make_unique<socket> (get_io_context ());
		}

		return *m_socket;
	}

	void on_handshake (const boost::system::error_code & error_code)
	{
		std::cout << "ssl_client::on_handshake -- error_code = " << error_code << std::endl;

		if (error_code)
		{
			if (336130315 != error_code.value ())
			{
				return m_ssl_socket->add_error (error_code);
			}

			std::cout << "ssl_client::on_handshake -- probably talking to a plain server, retrying" << std::endl;

			const auto port = m_ssl_socket->get ().remote_endpoint ().port ();
			close ();

			m_try_ssl = false;
			run (port);
		}
		else
		{
			std::cout << "ssl_client::on_handshake -- probably talking to a secure server, marking socket as connected" << std::endl;
			m_ssl_socket->mark_as_connected ();
		}
	}
};

auto build_mixed_servers (boost::asio::io_context & io_context)
{
	std::vector<std::shared_ptr<server>> result{};
	result.push_back (std::make_shared<ssl_server> (io_context));
	result.push_back (std::make_shared<plain_server> (io_context));
	result.push_back (std::make_shared<ssl_server> (io_context));
	result.push_back (std::make_shared<plain_server> (io_context));

	return result;
}

auto build_mixed_clients (boost::asio::io_context & io_context)
{
	std::vector<std::shared_ptr<client>> result{};
	result.push_back (std::make_shared<ssl_client> (io_context));
	result.push_back (std::make_shared<plain_client> (io_context));
	result.push_back (std::make_shared<ssl_client> (io_context));
	result.push_back (std::make_shared<plain_client> (io_context));

	return result;
}

auto build_mixed_connection_entities (boost::asio::io_context & io_context)
{
	return std::make_pair (build_mixed_servers (io_context), build_mixed_clients (io_context));
}

}
