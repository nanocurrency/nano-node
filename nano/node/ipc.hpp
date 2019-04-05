#pragma once

#include <atomic>
#include <nano/lib/ipc.hpp>
#include <nano/lib/jsonconfig.hpp>
#include <vector>

namespace nano
{
class node;
class rpc;

namespace ipc
{
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
}
}
