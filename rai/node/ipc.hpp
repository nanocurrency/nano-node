#pragma once

#include <atomic>
#include <boost/property_tree/ptree.hpp>
#include <rai/lib/errors.hpp>
#include <rai/node/api.hpp>
#include <string>
#include <vector>

namespace rai
{
class node;
}

namespace nano
{
/** IPC errors. Do not change or reuse enum values as these propagate to clients. */
enum class error_ipc
{
	generic = 1,
	invalid_preamble = 2,
};

namespace ipc
{
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
		bool enabled{ false };
		bool control_enabled{ false };
		size_t io_timeout{ 15 };
		size_t io_threads;
	};

	/** Domain socket specific transport config */
	class ipc_config_domain_socket : public ipc_config_transport
	{
	public:
		/**
		 * Default domain socket path for Unix systems. Once we support Windows 10 usocks, this value
		 * will be conditional on OS.
		 */
		std::string path{ "/tmp/nano" };
	};

	/** TCP specific transport config */
	class ipc_config_tcp_socket : public ipc_config_transport
	{
	public:
		std::string address{ "::1" };
		uint16_t port{ 7077 };
	};

	/** IPC configuration */
	class ipc_config
	{
	public:
		/** Reads the JSON "ipc" node from the config, if present */
		bool deserialize_json (boost::property_tree::ptree & tree_a);
		ipc_config_domain_socket transport_domain;
		ipc_config_tcp_socket transport_tcp;
	};

	/** IPC server */
	class ipc_server
	{
	public:
		ipc_server (rai::node & node);
		~ipc_server ();
		void stop ();

	private:
		rai::node & node;
		nano::api::api_handler handler;
		std::atomic<bool> stopped;
		std::unique_ptr<dsock_file_remover> file_remover;
		std::vector<std::shared_ptr<nano::ipc::transport>> transports;
	};
}
}

REGISTER_ERROR_CODES (nano, error_ipc)
