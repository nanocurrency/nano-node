#include <celerix/ipc_flatbuffers_lib/generated/flatbuffers/celerixapi_generated.h>
#include <celerix/lib/errors.hpp>
#include <celerix/lib/numbers.hpp>
#include <celerix/node/ipc/action_handler.hpp>
#include <celerix/node/ipc/ipc_server.hpp>
#include <celerix/node/node.hpp>

namespace
{
celerix::account parse_account (std::string const & account, bool & out_is_deprecated_format)
{
	celerix::account result{};
	if (account.empty ())
	{
		throw celerix::error (celerix::error_common::bad_account_number);
	}

	if (result.decode_account (account))
	{
		throw celerix::error (celerix::error_common::bad_account_number);
	}
	else if (account[3] == '-' || account[4] == '-')
	{
		out_is_deprecated_format = true;
	}

	return result;
}
/** Returns the message as a Flatbuffers ObjectAPI type, managed by a unique_ptr */
template <typename T>
auto get_message (celerixapi::Envelope const & envelope)
{
	auto raw (envelope.message_as<T> ()->UnPack ());
	return std::unique_ptr<typename T::NativeTableType> (raw);
}
}

/**
 * Mapping from message type to handler function.
 * @note This must be updated whenever a new message type is added to the Flatbuffers IDL.
 */
auto celerix::ipc::action_handler::handler_map () -> std::unordered_map<celerixapi::Message, std::function<void (celerix::ipc::action_handler *, celerixapi::Envelope const &)>, celerix::ipc::enum_hash>
{
	static std::unordered_map<celerixapi::Message, std::function<void (celerix::ipc::action_handler *, celerixapi::Envelope const &)>, celerix::ipc::enum_hash> handlers;
	if (handlers.empty ())
	{
		handlers.emplace (celerixapi::Message::Message_IsAlive, &celerix::ipc::action_handler::on_is_alive);
		handlers.emplace (celerixapi::Message::Message_TopicConfirmation, &celerix::ipc::action_handler::on_topic_confirmation);
		handlers.emplace (celerixapi::Message::Message_AccountWeight, &celerix::ipc::action_handler::on_account_weight);
		handlers.emplace (celerixapi::Message::Message_ServiceRegister, &celerix::ipc::action_handler::on_service_register);
		handlers.emplace (celerixapi::Message::Message_ServiceStop, &celerix::ipc::action_handler::on_service_stop);
		handlers.emplace (celerixapi::Message::Message_TopicServiceStop, &celerix::ipc::action_handler::on_topic_service_stop);
	}
	return handlers;
}

celerix::ipc::action_handler::action_handler (celerix::node & node_a, celerix::ipc::ipc_server & server_a, std::weak_ptr<celerix::ipc::subscriber> const & subscriber_a, std::shared_ptr<flatbuffers::FlatBufferBuilder> const & builder_a) :
	flatbuffer_producer (builder_a),
	node (node_a),
	ipc_server (server_a),
	subscriber (subscriber_a)
{
}

void celerix::ipc::action_handler::on_topic_confirmation (celerixapi::Envelope const & envelope_a)
{
	auto confirmationTopic (get_message<celerixapi::TopicConfirmation> (envelope_a));
	ipc_server.get_broker ()->subscribe (subscriber, std::move (confirmationTopic));
	celerixapi::EventAckT ack;
	create_response (ack);
}

void celerix::ipc::action_handler::on_service_register (celerixapi::Envelope const & envelope_a)
{
	require_oneof (envelope_a, { celerix::ipc::access_permission::api_service_register, celerix::ipc::access_permission::service });
	auto query (get_message<celerixapi::ServiceRegister> (envelope_a));
	ipc_server.get_broker ()->service_register (query->service_name, this->subscriber);
	celerixapi::SuccessT success;
	create_response (success);
}

void celerix::ipc::action_handler::on_service_stop (celerixapi::Envelope const & envelope_a)
{
	require_oneof (envelope_a, { celerix::ipc::access_permission::api_service_stop, celerix::ipc::access_permission::service });
	auto query (get_message<celerixapi::ServiceStop> (envelope_a));
	if (query->service_name == "node")
	{
		ipc_server.node.stop ();
	}
	else
	{
		ipc_server.get_broker ()->service_stop (query->service_name);
	}
	celerixapi::SuccessT success;
	create_response (success);
}

void celerix::ipc::action_handler::on_topic_service_stop (celerixapi::Envelope const & envelope_a)
{
	auto topic (get_message<celerixapi::TopicServiceStop> (envelope_a));
	ipc_server.get_broker ()->subscribe (subscriber, std::move (topic));
	celerixapi::EventAckT ack;
	create_response (ack);
}

void celerix::ipc::action_handler::on_account_weight (celerixapi::Envelope const & envelope_a)
{
	require_oneof (envelope_a, { celerix::ipc::access_permission::api_account_weight, celerix::ipc::access_permission::account_query });
	bool is_deprecated_format{ false };
	auto query (get_message<celerixapi::AccountWeight> (envelope_a));
	auto balance (node.weight (parse_account (query->account, is_deprecated_format)));

	celerixapi::AccountWeightResponseT response;
	response.voting_weight = balance.str ();
	create_response (response);
}

void celerix::ipc::action_handler::on_is_alive (celerixapi::Envelope const & envelope)
{
	celerixapi::IsAliveT alive;
	create_response (alive);
}

bool celerix::ipc::action_handler::has_access (celerixapi::Envelope const & envelope_a, celerix::ipc::access_permission permission_a) const noexcept
{
	// If credentials are missing in the envelope, the default user is used
	std::string credentials;
	if (envelope_a.credentials () != nullptr)
	{
		credentials = envelope_a.credentials ()->str ();
	}

	return ipc_server.get_access ().has_access (credentials, permission_a);
}

bool celerix::ipc::action_handler::has_access_to_all (celerixapi::Envelope const & envelope_a, std::initializer_list<celerix::ipc::access_permission> permissions_a) const noexcept
{
	// If credentials are missing in the envelope, the default user is used
	std::string credentials;
	if (envelope_a.credentials () != nullptr)
	{
		credentials = envelope_a.credentials ()->str ();
	}

	return ipc_server.get_access ().has_access_to_all (credentials, permissions_a);
}

bool celerix::ipc::action_handler::has_access_to_oneof (celerixapi::Envelope const & envelope_a, std::initializer_list<celerix::ipc::access_permission> permissions_a) const noexcept
{
	// If credentials are missing in the envelope, the default user is used
	std::string credentials;
	if (envelope_a.credentials () != nullptr)
	{
		credentials = envelope_a.credentials ()->str ();
	}

	return ipc_server.get_access ().has_access_to_oneof (credentials, permissions_a);
}

void celerix::ipc::action_handler::require (celerixapi::Envelope const & envelope_a, celerix::ipc::access_permission permission_a) const
{
	if (!has_access (envelope_a, permission_a))
	{
		throw celerix::error (celerix::error_common::access_denied);
	}
}

void celerix::ipc::action_handler::require_all (celerixapi::Envelope const & envelope_a, std::initializer_list<celerix::ipc::access_permission> permissions_a) const
{
	if (!has_access_to_all (envelope_a, permissions_a))
	{
		throw celerix::error (celerix::error_common::access_denied);
	}
}

void celerix::ipc::action_handler::require_oneof (celerixapi::Envelope const & envelope_a, std::initializer_list<celerix::ipc::access_permission> permissions_a) const
{
	if (!has_access_to_oneof (envelope_a, permissions_a))
	{
		throw celerix::error (celerix::error_common::access_denied);
	}
}
