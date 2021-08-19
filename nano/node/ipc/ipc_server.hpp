#pragma once

#include <nano/lib/errors.hpp>
#include <nano/node/ipc/ipc_access_config.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace nano
{
class node;
class node_rpc_config;
namespace ipc
{
	class broker;
	class dsock_file_remover;
	class transport;
	/** The IPC server accepts connections on one or more configured transports */
	class ipc_server final
	{
	public:
		ipc_server (nano::node & node, nano::node_rpc_config const & node_rpc_config);
		~ipc_server ();
		void stop ();

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
