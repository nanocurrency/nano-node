#pragma once

#include <nano/lib/config.hpp>
#include <nano/lib/errors.hpp>

#include <string>

namespace nano
{
class jsonconfig;

namespace ipc
{
	/** Base class for transport configurations */
	class ipc_config_transport
	{
	public:
		virtual ~ipc_config_transport () = default;
		bool enabled{ false };
		bool allow_unsafe{ false };
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

		int json_version () const
		{
			return 1;
		}
	};

	/** TCP specific transport config */
	class ipc_config_tcp_socket : public ipc_config_transport
	{
	public:
		ipc_config_tcp_socket () :
		port (network_constants.default_ipc_port)
		{
		}
		nano::network_constants network_constants;
		/** Listening port */
		uint16_t port;
	};

	/** IPC configuration */
	class ipc_config
	{
	public:
		nano::error deserialize_json (bool & upgraded_a, nano::jsonconfig & json_a);
		nano::error serialize_json (nano::jsonconfig & json) const;
		ipc_config_domain_socket transport_domain;
		ipc_config_tcp_socket transport_tcp;
	};
}
}
