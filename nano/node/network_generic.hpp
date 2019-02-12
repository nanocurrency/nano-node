#pragma once

#include <boost/asio.hpp>
#include <boost/variant.hpp>
#include <chrono>
#include <crypto/xxhash/xxhash.h>
#include <memory>
#include <mutex>
#include <nano/lib/numbers.hpp>

namespace nano
{
class node;
class message;
namespace net
{
	/** Supported protocols. These have explicit values as they may be serialized. */
	enum class protocol : uint8_t
	{
		unknown = 0,
		udp = 1,
		tcp = 2
	};

	/**
	 * Encapsulates an ip address, port and protocol with conversion to and from Boost endpoint types.
	 * The socket_addr can be converted between protocol types using as_<type> ()
	 * @todo The static factory functions are for convenience in tests, but we might want to replace them with additional constructors.
	 */
	class socket_addr
	{
	public:
		static socket_addr tcp_map_to_v6 (nano::net::socket_addr const & endpoint_a);
		static socket_addr udp_map_to_v6 (nano::net::socket_addr const & endpoint_a);
		/** Create a tcp endpoint from ip address and port number */
		static socket_addr make_tcp (const boost::asio::ip::address & address_a, unsigned short port_a);
		/** Create a udp endpoint from ip address and port number */
		static socket_addr make_udp (const boost::asio::ip::address & address_a, unsigned short port_a);
		/** Parse ip address and port into a tcp endpoint */
		static socket_addr make_tcp (std::string address_a, bool & parse_error_a);
		/** Parse ip address and port into a udp endpoint */
		static socket_addr make_udp (std::string address_a, bool & parse_error_a);
		/** Creates an endpoint equivalent to a default-constructed boost::asio::ip::tcp::endpoint */
		static socket_addr make_default_tcp ();
		/** Creates an endpoint equivalent to a default-constructed boost::asio::ip::udp::endpoint */
		static socket_addr make_default_udp ();

		socket_addr () = default;

		/** Construct based on IP address and port number */
		socket_addr (const boost::asio::ip::address & addr, unsigned short port_a, nano::net::protocol protocol_a = nano::net::protocol::udp)
		{
			if (protocol_a == nano::net::protocol::udp)
			{
				*this = make_udp (addr, port_a);
			}
			else if (protocol_a == nano::net::protocol::tcp)
			{
				*this = make_tcp (addr, port_a);
			}
			else
			{
				assert (false);
			}
		}

		/** Returns an ipv6 mapped copy */
		socket_addr map_to_v6 () const;

		/** Convert this endpoint to another protocol type. This is a no-op if the protocol type is already used. */
		void convert_to (nano::net::protocol protocol_a)
		{
			if (protocol_a == nano::net::protocol::tcp && endpoint.which () != type_tcp)
			{
				*this = make_tcp (address (), port ());
			}
			else if (protocol_a == nano::net::protocol::udp && endpoint.which () != type_udp)
			{
				*this = make_udp (address (), port ());
			}
		}

		/** Get a copy, enforcing tcp protocol */
		socket_addr as_tcp () const
		{
			auto result (*this);
			result.convert_to (nano::net::protocol::tcp);
			return result;
		}

		/** Get a copy, enforcing udp protocol */
		socket_addr as_udp () const
		{
			auto result (*this);
			result.convert_to (nano::net::protocol::udp);
			return result;
		}
		socket_addr (socket_addr const & other_a)
		{
			endpoint = other_a.endpoint;
		}
		socket_addr (boost::asio::ip::udp::endpoint const & udp)
		{
			endpoint = udp;
		}
		socket_addr (boost::asio::ip::udp::endpoint && udp)
		{
			endpoint = std::move (udp);
		}
		socket_addr (boost::asio::ip::tcp::endpoint const & tcp)
		{
			endpoint = tcp;
		}
		socket_addr (boost::asio::ip::tcp::endpoint && tcp)
		{
			endpoint = std::move (tcp);
		}
		explicit operator boost::asio::ip::tcp::endpoint () const
		{
			return tcp ();
		}
		explicit operator boost::asio::ip::udp::endpoint () const
		{
			return udp ();
		}
		socket_addr & operator= (const socket_addr & other)
		{
			endpoint = other.endpoint;
			return *this;
		}
		socket_addr & operator= (socket_addr && other)
		{
			endpoint = other.endpoint;
			return *this;
		}
		socket_addr & operator= (const boost::asio::ip::udp::endpoint & other)
		{
			endpoint = other;
			return *this;
		}
		socket_addr & operator= (boost::asio::ip::udp::endpoint && other)
		{
			endpoint = other;
			return *this;
		}
		socket_addr & operator= (const boost::asio::ip::tcp::endpoint & other)
		{
			endpoint = other;
			return *this;
		}
		socket_addr & operator= (boost::asio::ip::tcp::endpoint && other)
		{
			endpoint = other;
			return *this;
		}
		/** Compare two endpoints for equality. */
		friend bool operator== (const socket_addr & e1,
		const socket_addr & e2)
		{
			if (e1.is_udp () && e2.is_udp ())
			{
				return e1.udp () == e2.udp ();
			}
			else if (e1.is_tcp () && e2.is_tcp ())
			{
				return e1.tcp () == e2.tcp ();
			}
			else
			{
				return false;
			}
		}

		/** Compare two endpoints for inequality. */
		friend bool operator!= (const socket_addr & e1,
		const socket_addr & e2)
		{
			if (e1.is_udp () && e2.is_udp ())
			{
				return !(e1.udp () == e2.udp ());
			}
			else if (e1.is_tcp () && e2.is_tcp ())
			{
				return !(e1.tcp () == e2.tcp ());
			}
			else
			{
				return true;
			}
		}

		/** Compare endpoints for ordering. If endpoints are of different types, the tcp endpoint is deemed smaller. */
		friend bool operator< (const socket_addr & e1,
		const socket_addr & e2)
		{
			if (e1.is_udp () && e2.is_udp ())
			{
				return e1.udp () < e2.udp ();
			}
			else if (e1.is_tcp () && e2.is_tcp ())
			{
				return e1.tcp () < e2.tcp ();
			}
			else
			{
				return e1.is_tcp ();
			}
		}

		/** Compare endpoints for ordering. */
		friend bool operator> (const socket_addr & e1,
		const socket_addr & e2)
		{
			return e2 < e1;
		}

		/** Compare endpoints for ordering. */
		friend bool operator<= (const socket_addr & e1,
		const socket_addr & e2)
		{
			return !(e2 < e1);
		}

		/** Compare endpoints for ordering. */
		friend bool operator>= (const socket_addr & e1,
		const socket_addr & e2)
		{
			return !(e1 < e2);
		}
		bool is_udp () const
		{
			return endpoint.which () == type_udp;
		}
		bool is_tcp () const
		{
			return endpoint.which () == type_tcp;
		}

		void set (boost::asio::ip::udp::endpoint endpoint_a)
		{
			endpoint = endpoint_a;
		}

		void set (boost::asio::ip::tcp::endpoint endpoint_a)
		{
			endpoint = endpoint_a;
		}

		/** Returns the socket address as a UDP endpoint, converting from other endpoint types if necessary */
		boost::asio::ip::udp::endpoint udp () const
		{
			if (is_udp ())
			{
				return boost::get<boost::asio::ip::udp::endpoint> (endpoint);
			}
			else
			{
				return as_udp ().udp ();
			}
		}

		/** Returns the socket address as a TCP endpoint, converting from other endpoint types if necessary */
		boost::asio::ip::tcp::endpoint tcp () const
		{
			if (is_tcp ())
			{
				return boost::get<boost::asio::ip::tcp::endpoint> (endpoint);
			}
			else
			{
				return as_tcp ().tcp ();
			}
		}

		/** Get the port associated with the endpoint. The port number is always in the host's byte order. If the endpoint is invalid, 0 is returnd. */
		unsigned short port () const
		{
			if (is_udp ())
			{
				return udp ().port ();
			}
			else if (is_tcp ())
			{
				return tcp ().port ();
			}
			else
			{
				return 0;
			}
		}

		/** Set the port associated with the endpoint. The port number is always in the host's byte order. */
		void port (unsigned short port_num)
		{
			if (is_udp ())
			{
				auto ep (udp ());
				ep.port (port_num);
				set (ep);
			}
			else if (is_tcp ())
			{
				auto ep (tcp ());
				ep.port (port_num);
				set (ep);
			}
			else
			{
				assert (false);
			}
		}

		/** Get the IP address associated with the endpoint. If the endpoint is invalid, the default address is returned. */
		boost::asio::ip::address address () const
		{
			boost::asio::ip::address addr;
			if (is_udp ())
			{
				return udp ().address ();
			}
			else if (is_tcp ())
			{
				return tcp ().address ();
			}
			else
			{
				return boost::asio::ip::address ();
			}
		}

		/** Set the IP address associated with the endpoint. */
		void address (const boost::asio::ip::address & addr)
		{
			if (is_udp ())
			{
				auto ep (udp ());
				ep.address (addr);
				set (ep);
			}
			else if (is_tcp ())
			{
				auto ep (tcp ());
				ep.address (addr);
				set (ep);
			}
			else
			{
				assert (false);
			}
		}

		boost::asio::ip::udp protocol_udp () const
		{
			assert (is_udp ());
			return udp ().protocol ();
		}

		boost::asio::ip::tcp protocol_tcp () const
		{
			assert (is_tcp ());
			return tcp ().protocol ();
		}

		/** True if the socket address contains a valid value */
		bool valid () const
		{
			return endpoint.which () != type_invalid;
		}

		/** Invalidates the socket address */
		void invalidate ()
		{
			endpoint = false;
		}

	private:
		boost::variant<bool, boost::asio::ip::udp::endpoint, boost::asio::ip::tcp::endpoint> endpoint;
		static constexpr auto type_invalid = static_cast<int> (nano::net::protocol::unknown);
		static constexpr auto type_udp = static_cast<int> (nano::net::protocol::udp);
		static constexpr auto type_tcp = static_cast<int> (nano::net::protocol::tcp);
	};

	/** Enable socket_addr to be used as arguments to ostreams, such as boost log */
	template <typename Elem, typename Traits>
	std::basic_ostream<Elem, Traits> & operator<< (std::basic_ostream<Elem, Traits> & os, const socket_addr & endpoint)
	{
		if (endpoint.is_udp ())
		{
			os << endpoint.udp ();
		}
		else
		{
			os << endpoint.tcp ();
		}
		return os;
	}

	/** Protocol-agnostic connection interface */
	class client
	{
	public:
		virtual void async_connect (nano::net::socket_addr const &, std::function<void(boost::system::error_code const &)>) = 0;
		virtual void async_read (uint8_t * buffer_a, size_t size, std::function<void(boost::system::error_code const &, size_t, nano::net::socket_addr const &)> callback_a) = 0;
		virtual void async_read (std::shared_ptr<std::vector<uint8_t>> buffer_a, size_t size_a, std::function<void(boost::system::error_code const &, size_t, nano::net::socket_addr const &)> callback_a) = 0;
		virtual void async_write (uint8_t const * buffer_a, size_t size, std::function<void(boost::system::error_code const &, size_t)> callback_a) = 0;
		virtual void async_write (std::shared_ptr<std::vector<uint8_t>> buffer_a, std::function<void(boost::system::error_code const &, size_t)> callback_a) = 0;
		virtual void close () = 0;
		virtual nano::net::socket_addr local_endpoint (boost::system::error_code & ec) = 0;
		virtual nano::net::socket_addr remote_endpoint (boost::system::error_code & ec) = 0;
		virtual nano::net::socket_addr local_endpoint ()
		{
			boost::system::error_code ec;
			return local_endpoint (ec);
		}
		virtual nano::net::socket_addr remote_endpoint ()
		{
			boost::system::error_code ec;
			return remote_endpoint (ec);
		}
		virtual ~client () = default;
	};

	/** Utilities shared between socket types */
	template <typename SOCKET_BASE>
	class socket_common
	{
	public:
		socket_common ()
		{
		}
		virtual typename SOCKET_BASE::socket & raw_socket () = 0;
		typename SOCKET_BASE::endpoint remote_endpoint (boost::system::error_code & ec)
		{
			typename SOCKET_BASE::endpoint endpoint;

			if (raw_socket ().is_open ())
			{
				endpoint = raw_socket ().remote_endpoint (ec);
			}

			return endpoint;
		}
		typename SOCKET_BASE::endpoint local_endpoint (boost::system::error_code & ec)
		{
			typename SOCKET_BASE::endpoint endpoint;

			if (raw_socket ().is_open ())
			{
				endpoint = raw_socket ().local_endpoint (ec);
			}

			return endpoint;
		}
		void close ()
		{
			if (raw_socket ().is_open ())
			{
				try
				{
					raw_socket ().shutdown (SOCKET_BASE::socket::shutdown_both);
				}
				catch (...)
				{
					/* Ignore spurious exceptions; shutdown is best effort. */
				}
				raw_socket ().close ();
			}
		}

	protected:
		std::chrono::steady_clock::time_point last_contact;
	};

	/** Client socket for TCP */
	class tcp_client : public socket_common<boost::asio::ip::tcp>, public client, public std::enable_shared_from_this<nano::net::tcp_client>
	{
	public:
		tcp_client (nano::node & node);
		void async_connect (nano::net::socket_addr const &, std::function<void(boost::system::error_code const &)>) override;
		void async_read (uint8_t * buffer_a, size_t size, std::function<void(boost::system::error_code const &, size_t, nano::net::socket_addr const &)>) override;
		void async_read (std::shared_ptr<std::vector<uint8_t>>, size_t, std::function<void(boost::system::error_code const &, size_t, nano::net::socket_addr const &)>) override;
		void async_write (uint8_t const * buffer_a, size_t size, std::function<void(boost::system::error_code const &, size_t)> callback_a) override;
		void async_write (std::shared_ptr<std::vector<uint8_t>>, std::function<void(boost::system::error_code const &, size_t)>) override;
		nano::net::socket_addr local_endpoint (boost::system::error_code & ec) override
		{
			return socket_common<boost::asio::ip::tcp>::local_endpoint (ec);
		}
		nano::net::socket_addr remote_endpoint (boost::system::error_code & ec) override
		{
			return socket_common<boost::asio::ip::tcp>::remote_endpoint (ec);
		}
		void close () override
		{
			socket_common<boost::asio::ip::tcp>::close ();
		}
		boost::asio::ip::tcp::socket & raw_socket () override
		{
			return socket_m;
		}

	private:
		void start (std::chrono::steady_clock::time_point = std::chrono::steady_clock::now () + std::chrono::seconds (5));
		void stop ();
		void checkup ();

		std::atomic<uint64_t> cutoff;
		nano::node & node;
		boost::asio::ip::tcp::socket socket_m;
	};

	/** Client socket for UDP */
	class udp_client : public socket_common<boost::asio::ip::udp>, public client, public std::enable_shared_from_this<nano::net::udp_client>
	{
	public:
		/**
		 * @param local_endpoint Endpoint to send from, typically udp::endpoint (boost::asio::ip::address_v6::any (), port)
		 */
		udp_client (nano::node & node, nano::net::socket_addr local_endpoint);

		/** UDP is connectionless; set remote endpoint (for writes) and invoke the callback synchronously. */
		void async_connect (nano::net::socket_addr const &, std::function<void(boost::system::error_code const &)>) override;
		void async_read (uint8_t * buffer_a, size_t size, std::function<void(boost::system::error_code const &, size_t, nano::net::socket_addr const &)>) override;
		void async_read (std::shared_ptr<std::vector<uint8_t>>, size_t, std::function<void(boost::system::error_code const &, size_t, nano::net::socket_addr const &)>) override;
		void async_write (uint8_t const * buffer_a, size_t size, std::function<void(boost::system::error_code const &, size_t)> callback_a) override;
		void async_write (std::shared_ptr<std::vector<uint8_t>>, std::function<void(boost::system::error_code const &, size_t)>) override;
		nano::net::socket_addr local_endpoint (boost::system::error_code & ec) override
		{
			return socket_common<boost::asio::ip::udp>::local_endpoint (ec);
		}
		nano::net::socket_addr remote_endpoint (boost::system::error_code & ec) override
		{
			return remote_endpoint_m;
		}
		void close () override
		{
			socket_common<boost::asio::ip::udp>::close ();
		}
		boost::asio::ip::udp::socket & raw_socket () override
		{
			return socket;
		}

	private:
		nano::node & node;
		boost::asio::ip::udp::socket socket;
		/** Endpoint we're sending to */
		boost::asio::ip::udp::endpoint remote_endpoint_m;
	};

	/*
	 	Everything below here is work in progress / exploring ideas
	*/

	/** Interface for receiving inbound messages */
	class message_processor
	{
	private:
		virtual void on_message (nano::message const & message) = 0;
	};

	class session_pool;

	/**
	 * A session is a high level interface for sending messages over any transport. A session maintains a connection
	 * along with metadata such as when there was last communication with the peer.
	 * @todo Pass session to tcp_client/udp_client so they can update last_activity, etc?
	 */
	class session
	{
	public:
		session (session_pool & pool_a);

		/** Send message to the peer */
		std::error_code send_message (nano::message const & message);

		/** Close the session and the underlying connection. */
		std::error_code close ();

	private:
		session_pool & pool;
		std::shared_ptr<nano::net::client> connection;
		std::chrono::steady_clock::time_point last_activity;
	};

	/** Manages a set of peer sessions */
	class session_pool
	{
	public:
		/** Get a session by address, create a new one if necessary */
		std::shared_ptr<session> get_session (nano::net::socket_addr & addr)
		{
			// todo: look up by address. do a bit of cleanup.
			return nullptr;
		}

		/** If a session is inactive for longer than the limit, it's eligble for removal. Future attempts will cause a reconnection. */
		static constexpr std::chrono::minutes inactivity_limit{ 5 };
		// TODO: a multi-index container with last-activity index, etc
	};

	/** Abstract interface for servers */
	class server
	{
	public:
		virtual void start () = 0;
		virtual void stop () = 0;
	};

	class tcp_server : public server
	{
	public:
		tcp_server (std::shared_ptr<nano::node> port, uint16_t listening_port);
		void start () override;
		void stop () override;

	protected:
		/** This is implemented by subclasses to handle new connections. */
		void accept_action (boost::system::error_code const & ec, std::shared_ptr<nano::net::client> socket_a);

	private:
		void accept ();
	};

	class udp_server : public server
	{
	public:
		void start () override;
		void stop () override;
	};
}
}

namespace
{
struct remote_hash
{
	std::size_t operator() (::nano::net::socket_addr const & endpoint_a) const noexcept
	{
		assert (endpoint_a.address ().is_v6 ());
		nano::uint128_union address;
		address.bytes = endpoint_a.address ().to_v6 ().to_bytes ();
		XXH64_state_t * const state = XXH64_createState ();
		XXH64_reset (state, 0);
		XXH64_update (state, address.bytes.data (), address.bytes.size ());
		auto port (endpoint_a.port ());
		XXH64_update (state, &port, sizeof (port));
		auto result (XXH64_digest (state));
		XXH64_freeState (state);
		return static_cast<std::size_t> (result);
	}
};
}
namespace std
{
template <>
struct hash<::nano::net::socket_addr>
{
	std::size_t operator() (::nano::net::socket_addr const & endpoint_a) const
	{
		::remote_hash rhash;
		return rhash (endpoint_a);
	}
};
}

/** Inject hash into the boost namespace for the multi_index container */
namespace boost
{
template <>
struct hash<::nano::net::socket_addr>
{
	size_t operator() (::nano::net::socket_addr const & endpoint_a) const
	{
		std::hash<::nano::net::socket_addr> hash;
		return hash (endpoint_a);
	}
};
}
