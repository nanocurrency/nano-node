#pragma once

#include <functional>
#include <memory>
#include <sstream>
#include <string>

namespace nano
{
class rpc;

/** Keeps information about http requests, and for v2+ includes path and header values of interest */
class rpc_handler_request_params final
{
public:
	int rpc_version{ 1 };
	std::string path;
	std::string credentials;
	std::string correlation_id;

	/**
	 * If the path is non-empty, this wraps the body inside an IPC API compliant envelope.
	 * Otherwise the input string is returned unchanged.
	 * This allows HTTP clients to use a simplified request format by omitting the envelope.
	 * Envelope fields may still be specified through corresponding nano- header fields.
	 */
	std::string json_envelope (std::string const & body_a) const
	{
		std::string body_l;
		if (!path.empty ())
		{
			std::ostringstream json;
			json << "{";
			if (!credentials.empty ())
			{
				json << "\"credentials\": \"" << credentials << "\", ";
			}
			if (!correlation_id.empty ())
			{
				json << "\"correlation_id\": \"" << correlation_id << "\", ";
			}
			json << "\"message_type\": \"" << path << "\", ";
			json << "\"message\": " << body_a;
			json << "}";
			body_l = json.str ();
		}
		else
		{
			body_l = body_a;
		}
		return body_l;
	}
};

class rpc_handler_interface
{
public:
	virtual ~rpc_handler_interface () = default;
	/** Process RPC 1.0 request. */
	virtual void process_request (std::string const & action, std::string const & body, std::function<void (std::string const &)> response) = 0;
	/** Process RPC 2.0 request. This is called via the IPC API */
	virtual void process_request_v2 (rpc_handler_request_params const & params_a, std::string const & body, std::function<void (std::shared_ptr<std::string> const &)> response) = 0;
	virtual void stop () = 0;
	virtual void rpc_instance (nano::rpc & rpc) = 0;
};
}
