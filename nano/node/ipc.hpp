#pragma once

#include <atomic>
#include <boost/asio.hpp>
#include <boost/property_tree/ptree.hpp>
#include <nano/lib/errors.hpp>
#include <nano/lib/jsonconfig.hpp>
#include <string>
#include <vector>

namespace nano
{
class node;
class rpc;
}

namespace nano
{
namespace ipc
{
	/**
	 * Payload encodings; add protobuf, flatbuffers and so on as needed.
	 */
	enum class payload_encoding : uint8_t
	{
		/**
		 * Request is preamble followed by 32-bit BE payload length and payload bytes.
		 * Response is 32-bit BE payload length followed by payload bytes.
		 */
		json_legacy = 1
	};

	/** Removes domain socket files on startup and shutdown */
	class dsock_file_remover;

	/** IPC transport interface */
	class transport
	{
	public:
		virtual void stop () = 0;
		virtual ~transport () = default;
	};

	/** Base class for transport configurations */
	class ipc_config_transport
	{
	public:
		virtual ~ipc_config_transport () = default;
		bool enabled{ false };
		size_t io_timeout{ 15 };
		long io_threads{ -1 };
	};

	/** Domain socket specific transport config */
	class ipc_config_domain_socket : public ipc_config_transport
	{
	public:
		/**
		 * Default domain socket path for Unix systems. Once Boost supports Windows 10 usocks,
		 * this value will be conditional on OS.
		 */
		std::string path{ "/tmp/nano" };
	};

	/** TCP specific transport config */
	class ipc_config_tcp_socket : public ipc_config_transport
	{
	public:
		/** Listening port */
		uint16_t port{ 7077 };
	};

	/** IPC configuration */
	class ipc_config
	{
	public:
		nano::error deserialize_json (nano::jsonconfig & json_a);
		nano::error serialize_json (nano::jsonconfig & json) const;
		ipc_config_domain_socket transport_domain;
		ipc_config_tcp_socket transport_tcp;
	};

	/** The IPC server accepts connections on one or more configured transports */
	class ipc_server
	{
	public:
		ipc_server (nano::node & node, nano::rpc & rpc);
		virtual ~ipc_server ();
		void stop ();

		nano::node & node;
		nano::rpc & rpc;

		/** Unique counter/id shared across sessions */
		std::atomic<uint64_t> id_dispenser{ 0 };

	private:
		std::unique_ptr<dsock_file_remover> file_remover;
		std::vector<std::shared_ptr<nano::ipc::transport>> transports;
	};

	class ipc_client_impl
	{
	public:
		virtual ~ipc_client_impl () = default;
	};

	/** IPC client */
	class ipc_client
	{
	public:
		ipc_client (boost::asio::io_context & io_ctx_a);
		virtual ~ipc_client () = default;

		/** Connect to a domain socket */
		nano::error connect (std::string const & path);

		/** Connect to a tcp socket synchronously */
		nano::error connect (std::string const & host, uint16_t port);

		/** Connect to a tcp socket asynchronously */
		void async_connect (std::string const & host, uint16_t port, std::function<void(nano::error)> callback);

		/** Write buffer asynchronously */
		void async_write (std::shared_ptr<std::vector<uint8_t>> buffer_a, std::function<void(nano::error, size_t)> callback_a);

		/** Read \p size_a bytes asynchronously */
		void async_read (std::shared_ptr<std::vector<uint8_t>> buffer_a, size_t size_a, std::function<void(nano::error, size_t)> callback_a);

		/**
		 * Returns a buffer with an IPC preamble for the given \p encoding_a followed by the payload. Depending on encoding,
		 * the buffer may contain a payload length or end sentinel.
		 */
		std::shared_ptr<std::vector<uint8_t>> prepare_request (nano::ipc::payload_encoding encoding_a, std::string const & payload_a);

	private:
		boost::asio::io_context & io_ctx;

		// PIMPL pattern to hide implementation details
		std::unique_ptr<ipc_client_impl> impl;
	};

	/** Convenience wrapper for making synchronous RPC calls via IPC */
	class rpc_ipc_client : public ipc_client
	{
	public:
		rpc_ipc_client (boost::asio::io_context & io_ctx_a) :
		ipc_client (io_ctx_a)
		{
		}
		/** Calls the RPC server via IPC and waits for the result. The client must be connected. */
		std::string request (std::string const & rpc_action_a);
	};
}
}
