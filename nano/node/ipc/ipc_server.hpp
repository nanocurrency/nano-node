#pragma once

#include <celerix/lib/errors.hpp>
#include <celerix/lib/ipc.hpp>
#include <celerix/lib/logging.hpp>
#include <celerix/node/ipc/ipc_access_config.hpp>
#include <celerix/node/ipc/ipc_broker.hpp>
#include <celerix/node/node_rpc_config.hpp>

#include <boost/asio/signal_set.hpp>

#include <atomic>
#include <memory>

namespace celerix
{
class node;
class error;
namespace ipc
{
	class access;
	/** The IPC server accepts connections on one or more configured transports */
	class ipc_server final : public std::enable_shared_from_this<ipc_server>
	{
	public:
		ipc_server (celerix::node & node, celerix::node_rpc_config const & node_rpc_config);
		~ipc_server ();
		void stop ();

		std::optional<std::uint16_t> listening_tcp_port () const;

		celerix::node & node;
		celerix::node_rpc_config const & node_rpc_config;

		/** Unique counter/id shared across sessions */
		std::atomic<uint64_t> id_dispenser{ 1 };
		std::shared_ptr<celerix::ipc::broker> get_broker ();
		celerix::ipc::access & get_access ();
		celerix::error reload_access_config ();

		celerix::logger logger{ "ipc_server" };

	private:
		void
		setup_callbacks ();
		std::shared_ptr<celerix::ipc::broker> broker;
		celerix::ipc::access access;
		std::unique_ptr<dsock_file_remover> file_remover;
		std::vector<std::shared_ptr<celerix::ipc::transport>> transports;
		std::shared_ptr<boost::asio::signal_set> signals;
	};
}
}
