#pragma once

#include <nano/lib/ipc.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/node/node_rpc_config.hpp>

#include <atomic>

namespace nano
{
class node;

namespace ipc
{
	/** The IPC server accepts connections on one or more configured transports */
	class ipc_server
	{
	public:
		ipc_server (nano::node & node_a, nano::node_rpc_config const & node_rpc_config);

		virtual ~ipc_server ();
		void stop ();

		nano::node & node;
		nano::node_rpc_config const & node_rpc_config;

		/** Unique counter/id shared across sessions */
		std::atomic<uint64_t> id_dispenser{ 0 };

	private:
		std::unique_ptr<dsock_file_remover> file_remover;
		std::vector<std::shared_ptr<nano::ipc::transport>> transports;
	};
}
}
