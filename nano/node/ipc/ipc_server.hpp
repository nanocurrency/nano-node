#pragma once

#include <nano/lib/errors.hpp>
#include <nano/lib/ipc.hpp>
#include <nano/node/ipc/ipc_access_config.hpp>
#include <nano/node/ipc/ipc_broker.hpp>
#include <nano/node/node_rpc_config.hpp>

#include <atomic>
#include <memory>

namespace flatbuffers
{
class Parser;
}
namespace nano
{
class node;
class error;
namespace ipc
{
	class access;
	/** The IPC server accepts connections on one or more configured transports */
	class ipc_server final
	{
	public:
		ipc_server (nano::node & node, nano::node_rpc_config const & node_rpc_config);
		~ipc_server ();
		void stop ();

		std::optional<std::uint16_t> listening_tcp_port () const;

		nano::node & node;
		nano::node_rpc_config const & node_rpc_config;

		/** Unique counter/id shared across sessions */
		std::atomic<uint64_t> id_dispenser{ 1 };
		std::shared_ptr<nano::ipc::broker> get_broker ();
		nano::ipc::access & get_access ();
		nano::error reload_access_config ();

	private:
		void setup_callbacks ();
		std::shared_ptr<nano::ipc::broker> broker;
		nano::ipc::access access;
		std::unique_ptr<dsock_file_remover> file_remover;
		std::vector<std::shared_ptr<nano::ipc::transport>> transports;
	};
}
}
