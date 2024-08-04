#include <nano/lib/errors.hpp>
#include <nano/node/ipc/action_handler.hpp>
#include <nano/node/ipc/flatbuffers_handler.hpp>
#include <nano/node/ipc/ipc_config.hpp>
#include <nano/node/ipc/ipc_server.hpp>

#include <boost/dll.hpp>
#include <boost/optional.hpp>

#include <iostream>
#include <sstream>

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/idl.h>
#include <flatbuffers/util.h>

namespace
{
auto handler_map = nano::ipc::action_handler::handler_map ();

/**
 * A helper for when it's necessary to create a JSON error response manually
 */
std::string make_error_response (std::string const & error_message)
{
	std::ostringstream json;
	json << R"json({"message_type": "Error", "message": {"code": 1, "message": ")json"
		 << error_message
		 << R"json("}})json";
	return json.str ();
}

/**
 * Returns the 'api/flatbuffers' directory, boost::none if not found.
 */
boost::optional<std::filesystem::path> get_api_path ()
{
	std::filesystem::path const fb_path = "api/flatbuffers";
	if (!std::filesystem::exists (fb_path))
	{
		return boost::none;
	}
	return fb_path;
}
}

nano::ipc::flatbuffers_handler::flatbuffers_handler (nano::node & node_a, nano::ipc::ipc_server & ipc_server_a, std::shared_ptr<nano::ipc::subscriber> const & subscriber_a, nano::ipc::ipc_config const & ipc_config_a) :
	node (node_a),
	ipc_server (ipc_server_a),
	subscriber (subscriber_a),
	ipc_config (ipc_config_a)
{
}

std::shared_ptr<flatbuffers::Parser> nano::ipc::flatbuffers_handler::make_flatbuffers_parser (nano::ipc::ipc_config const & ipc_config_a)
{
	auto parser (std::make_shared<flatbuffers::Parser> ());
	parser->opts.strict_json = true;
	parser->opts.skip_unexpected_fields_in_json = ipc_config_a.flatbuffers.skip_unexpected_fields_in_json;

	auto api_path = get_api_path ();
	if (!api_path)
	{
		throw nano::error ("Internal IPC error: unable to find api path");
	}

	const std::string api_path_str = api_path->string ();
	char const * include_directories[] = { api_path_str.c_str (), nullptr };
	std::string schemafile;
	if (!flatbuffers::LoadFile ((*api_path / "nanoapi.fbs").string ().c_str (), false, &schemafile))
	{
		throw nano::error ("Internal IPC error: unable to load schema file");
	}

	auto parse_success = parser->Parse (schemafile.c_str (), include_directories);
	if (!parse_success)
	{
		std::string parser_error = "Internal IPC error: unable to parse schema file: ";
		parser_error += parser->error_.c_str ();
		throw nano::error (parser_error);
	}
	return parser;
}

void nano::ipc::flatbuffers_handler::process_json (uint8_t const * message_buffer_a, std::size_t buffer_size_a,
std::function<void (std::shared_ptr<std::string> const &)> const & response_handler)
{
	try
	{
		if (!parser)
		{
			parser = make_flatbuffers_parser (ipc_config);
		}

		// Convert request from JSON
		auto body (std::string (reinterpret_cast<char *> (const_cast<uint8_t *> (message_buffer_a)), buffer_size_a));
		body += '\0';
		if (parser->Parse (reinterpret_cast<char const *> (body.data ())))
		{
			process (parser->builder_.GetBufferPointer (), parser->builder_.GetSize (), [parser = parser, response_handler] (std::shared_ptr<flatbuffers::FlatBufferBuilder> const & fbb) {
				// Convert response to JSON
				auto json (std::make_shared<std::string> ());
				if (!flatbuffers::GenText (*parser, fbb->GetBufferPointer (), json.get ()))
				{
					throw nano::error ("Couldn't serialize response to JSON");
				}

				response_handler (json);
			});
		}
		else
		{
			std::string parser_error = "Invalid message format: ";
			parser_error += parser->error_.c_str ();
			throw nano::error (parser_error);
		}
	}
	catch (nano::error const & err)
	{
		// Forces the parser construction to be retried as certain errors are
		// recoverable (such path errors getting fixed by the user without a node restart)
		parser = nullptr;

		// Convert error response to JSON. We must construct this manually since the exception
		// may be parser related (such as not being able to load the schema)
		response_handler (std::make_shared<std::string> (make_error_response (err.get_message ())));
	}
	catch (...)
	{
		std::cerr << "Unknown exception in " << __FUNCTION__ << std::endl;
		response_handler (std::make_shared<std::string> (make_error_response ("Unknown exception")));
	}
}

void nano::ipc::flatbuffers_handler::process (uint8_t const * message_buffer_a, std::size_t buffer_size_a,
std::function<void (std::shared_ptr<flatbuffers::FlatBufferBuilder> const &)> const & response_handler)
{
	auto buffer_l (std::make_shared<flatbuffers::FlatBufferBuilder> ());
	auto actionhandler (std::make_shared<action_handler> (node, ipc_server, subscriber, buffer_l));
	std::string correlationId = "";

	// Find and call the action handler
	try
	{
		// By default we verify the buffers, to make sure offsets reside inside the buffer.
		// This brings the buffer into cache, making the overall verify+parse overhead low.
		if (ipc_config.flatbuffers.verify_buffers)
		{
			auto verifier (flatbuffers::Verifier (message_buffer_a, buffer_size_a));
			if (!nanoapi::VerifyEnvelopeBuffer (verifier))
			{
				throw nano::error ("Envelope buffer did not pass verifier");
			}
		}

		auto incoming = nanoapi::GetEnvelope (message_buffer_a);
		if (incoming == nullptr)
		{
			nano::error err ("Invalid message");
			actionhandler->make_error (err.error_code_as_int (), err.get_message ());
			response_handler (buffer_l);
			return;
		}

		auto handler_method = handler_map.find (incoming->message_type ());
		if (handler_method != handler_map.end ())
		{
			if (incoming->correlation_id ())
			{
				actionhandler->set_correlation_id (incoming->correlation_id ()->str ());
			}
			handler_method->second (actionhandler.get (), *incoming);
		}
		else
		{
			nano::error err ("Unknown message type");
			actionhandler->make_error (err.error_code_as_int (), err.get_message ());
		}
	}
	catch (nano::error const & err)
	{
		actionhandler->make_error (err.error_code_as_int (), err.get_message ());
	}

	response_handler (buffer_l);
}
