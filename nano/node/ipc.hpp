#pragma once

#include <atomic>
#include <nano/lib/ipc.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/node/payment_observer_processor.hpp>

namespace nano
{
class node;

namespace ipc
{
	/** The IPC server accepts connections on one or more configured transports */
	class ipc_server
	{
	public:
		ipc_server (nano::node & node_a);

		virtual ~ipc_server ();
		void stop ();

		nano::node & node;

		/** Unique counter/id shared across sessions */
		std::atomic<uint64_t> id_dispenser{ 0 };

		nano::payment_observer_processor payment_observer_processor;

	private:
		std::unique_ptr<dsock_file_remover> file_remover;
		std::vector<std::shared_ptr<nano::ipc::transport>> transports;
	};
}
}
