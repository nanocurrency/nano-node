#include <nano/ipc_flatbuffers_lib/generated/flatbuffers/nanoapi_generated.h>
#include <nano/lib/errors.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/node/ipc/action_handler.hpp>
#include <nano/node/ipc/ipc_server.hpp>
#include <nano/node/node.hpp>

namespace
{
nano::account parse_account (std::string const & account, bool & out_is_deprecated_format)
{
	nano::account result{};
	if (account.empty ())
	{
		throw nano::error (nano::error_common::bad_account_number);
	}

	if (result.decode_account (account))
	{
		throw nano::error (nano::error_common::bad_account_number);
	}
	else if (account[3] == '-' || account[4] == '-')
	{
		out_is_deprecated_format = true;
	}

	return result;
}
/** Returns the message as a Flatbuffers ObjectAPI type, managed by a unique_ptr */
template <typename T>
auto get_message (nanoapi::Envelope const & envelope)
{
	auto raw (envelope.message_as<T> ()->UnPack ());
	return std::unique_ptr<typename T::NativeTableType> (raw);
}
}

/**
 * Mapping from message type to handler function.
 * @note This must be updated whenever a new message type is added to the Flatbuffers IDL.
 */
auto nano::ipc::action_handler::handler_map () -> std::unordered_map<nanoapi::Message, std::function<void (nano::ipc::action_handler *, nanoapi::Envelope const &)>, nano::ipc::enum_hash>
{
	static std::unordered_map<nanoapi::Message, std::function<void (nano::ipc::action_handler *, nanoapi::Envelope const &)>, nano::ipc::enum_hash> handlers;
	if (handlers.empty ())
	{
		handlers.emplace (nanoapi::Message::Message_IsAlive, &nano::ipc::action_handler::on_is_alive);
		handlers.emplace (nanoapi::Message::Message_TopicConfirmation, &nano::ipc::action_handler::on_topic_confirmation);
		handlers.emplace (nanoapi::Message::Message_AccountWeight, &nano::ipc::action_handler::on_account_weight);
		handlers.emplace (nanoapi::Message::Message_ServiceRegister, &nano::ipc::action_handler::on_service_register);
		handlers.emplace (nanoapi::Message::Message_ServiceStop, &nano::ipc::action_handler::on_service_stop);
		handlers.emplace (nanoapi::Message::Message_TopicServiceStop, &nano::ipc::action_handler::on_topic_service_stop);
	}
	return handlers;
}

nano::ipc::action_handler::action_handler (nano::node & node_a, nano::ipc::ipc_server & server_a, std::weak_ptr<nano::ipc::subscriber> const & subscriber_a, std::shared_ptr<flatbuffers::FlatBufferBuilder> const & builder_a) :
	flatbuffer_producer (builder_a),
	node (node_a),
	ipc_server (server_a),
	subscriber (subscriber_a)
{
}

void nano::ipc::action_handler::on_topic_confirmation (nanoapi::Envelope const & envelope_a)
{
	auto confirmationTopic (get_message<nanoapi::TopicConfirmation> (envelope_a));
	ipc_server.get_broker ()->subscribe (subscriber, std::move (confirmationTopic));
	nanoapi::EventAckT ack;
	create_response (ack);
}

void nano::ipc::action_handler::on_service_register (nanoapi::Envelope const & envelope_a)
{
	require_oneof (envelope_a, { nano::ipc::access_permission::api_service_register, nano::ipc::access_permission::service });
	auto query (get_message<nanoapi::ServiceRegister> (envelope_a));
	ipc_server.get_broker ()->service_register (query->service_name, this->subscriber);
	nanoapi::SuccessT success;
	create_response (success);
}

void nano::ipc::action_handler::on_service_stop (nanoapi::Envelope const & envelope_a)
{
	require_oneof (envelope_a, { nano::ipc::access_permission::api_service_stop, nano::ipc::access_permission::service });
	auto query (get_message<nanoapi::ServiceStop> (envelope_a));
	if (query->service_name == "node")
	{
		ipc_server.node.stop ();
	}
	else
	{
		ipc_server.get_broker ()->service_stop (query->service_name);
	}
	nanoapi::SuccessT success;
	create_response (success);
}

void nano::ipc::action_handler::on_topic_service_stop (nanoapi::Envelope const & envelope_a)
{
	auto topic (get_message<nanoapi::TopicServiceStop> (envelope_a));
	ipc_server.get_broker ()->subscribe (subscriber, std::move (topic));
	nanoapi::EventAckT ack;
	create_response (ack);
}

void nano::ipc::action_handler::on_account_weight (nanoapi::Envelope const & envelope_a)
{
	require_oneof (envelope_a, { nano::ipc::access_permission::api_account_weight, nano::ipc::access_permission::account_query });
	bool is_deprecated_format{ false };
	auto query (get_message<nanoapi::AccountWeight> (envelope_a));
	auto balance (node.weight (parse_account (query->account, is_deprecated_format)));

	nanoapi::AccountWeightResponseT response;
	response.voting_weight = balance.str ();
	create_response (response);
}

void nano::ipc::action_handler::on_is_alive (nanoapi::Envelope const & envelope)
{
	nanoapi::IsAliveT alive;
	create_response (alive);
}

bool nano::ipc::action_handler::has_access (nanoapi::Envelope const & envelope_a, nano::ipc::access_permission permission_a) const noexcept
{
	// If credentials are missing in the envelope, the default user is used
	std::string credentials;
	if (envelope_a.credentials () != nullptr)
	{
		credentials = envelope_a.credentials ()->str ();
	}

	return ipc_server.get_access ().has_access (credentials, permission_a);
}

bool nano::ipc::action_handler::has_access_to_all (nanoapi::Envelope const & envelope_a, std::initializer_list<nano::ipc::access_permission> permissions_a) const noexcept
{
	// If credentials are missing in the envelope, the default user is used
	std::string credentials;
	if (envelope_a.credentials () != nullptr)
	{
		credentials = envelope_a.credentials ()->str ();
	}

	return ipc_server.get_access ().has_access_to_all (credentials, permissions_a);
}

bool nano::ipc::action_handler::has_access_to_oneof (nanoapi::Envelope const & envelope_a, std::initializer_list<nano::ipc::access_permission> permissions_a) const noexcept
{
	// If credentials are missing in the envelope, the default user is used
	std::string credentials;
	if (envelope_a.credentials () != nullptr)
	{
		credentials = envelope_a.credentials ()->str ();
	}

	return ipc_server.get_access ().has_access_to_oneof (credentials, permissions_a);
}

void nano::ipc::action_handler::require (nanoapi::Envelope const & envelope_a, nano::ipc::access_permission permission_a) const
{
	if (!has_access (envelope_a, permission_a))
	{
		throw nano::error (nano::error_common::access_denied);
	}
}

void nano::ipc::action_handler::require_all (nanoapi::Envelope const & envelope_a, std::initializer_list<nano::ipc::access_permission> permissions_a) const
{
	if (!has_access_to_all (envelope_a, permissions_a))
	{
		throw nano::error (nano::error_common::access_denied);
	}
}

void nano::ipc::action_handler::require_oneof (nanoapi::Envelope const & envelope_a, std::initializer_list<nano::ipc::access_permission> permissions_a) const
{
	if (!has_access_to_oneof (envelope_a, permissions_a))
	{
		throw nano::error (nano::error_common::access_denied);
	}
}
