#pragma once

#include <atomic>
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
		size_t io_timeout{ 15 };
		size_t io_threads{ std::max (4u, std::thread::hardware_concurrency ()) };
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
		~ipc_server ();
		void stop ();

		nano::node & node;
		nano::rpc & rpc;

		/** Unique counter/id shared across sessions */
		std::atomic<uint64_t> id_dispenser{ 0 };

	private:
		std::atomic<bool> stopped;
		std::unique_ptr<dsock_file_remover> file_remover;
		std::vector<std::shared_ptr<nano::ipc::transport>> transports;
	};
}
}
