#pragma once

#include <celerix/ipc_flatbuffers_lib/flatbuffer_producer.hpp>
#include <celerix/ipc_flatbuffers_lib/generated/flatbuffers/celerixapi_generated.h>
#include <celerix/node/ipc/ipc_access_config.hpp>

#include <boost/optional.hpp>

#include <functional>
#include <memory>
#include <unordered_map>

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/idl.h>

namespace celerix
{
class error;
class node;
namespace ipc
{
	class ipc_server;
	class subscriber;

	/**
	 * Implements handlers for the various public IPC messages. When an action handler is completed,
	 * the flatbuffer contains the serialized response object.
	 * @note This is a light-weight class, and an instance can be created for every request.
	 */
	class action_handler final : public flatbuffer_producer, public std::enable_shared_from_this<action_handler>
	{
	public:
		action_handler (celerix::node & node, celerix::ipc::ipc_server & server, std::weak_ptr<celerix::ipc::subscriber> const & subscriber, std::shared_ptr<flatbuffers::FlatBufferBuilder> const & builder);

		void on_account_weight (celerixapi::Envelope const & envelope);
		void on_is_alive (celerixapi::Envelope const & envelope);
		void on_topic_confirmation (celerixapi::Envelope const & envelope);

		/** Request to register a service. The service name is associated with the current session. */
		void on_service_register (celerixapi::Envelope const & envelope);

		/** Request to stop a service by name */
		void on_service_stop (celerixapi::Envelope const & envelope);

		/** Subscribe to the ServiceStop event. The service must first have registered itself on the same session. */
		void on_topic_service_stop (celerixapi::Envelope const & envelope);

		/** Returns a mapping from api message types to handler functions */
		static auto handler_map () -> std::unordered_map<celerixapi::Message, std::function<void (action_handler *, celerixapi::Envelope const &)>, celerix::ipc::enum_hash>;

	private:
		bool has_access (celerixapi::Envelope const & envelope_a, celerix::ipc::access_permission permission_a) const noexcept;
		bool has_access_to_all (celerixapi::Envelope const & envelope_a, std::initializer_list<celerix::ipc::access_permission> permissions_a) const noexcept;
		bool has_access_to_oneof (celerixapi::Envelope const & envelope_a, std::initializer_list<celerix::ipc::access_permission> permissions_a) const noexcept;
		void require (celerixapi::Envelope const & envelope_a, celerix::ipc::access_permission permission_a) const;
		void require_all (celerixapi::Envelope const & envelope_a, std::initializer_list<celerix::ipc::access_permission> permissions_a) const;
		void require_oneof (celerixapi::Envelope const & envelope_a, std::initializer_list<celerix::ipc::access_permission> alternative_permissions_a) const;

		celerix::node & node;
		celerix::ipc::ipc_server & ipc_server;
		std::weak_ptr<celerix::ipc::subscriber> subscriber;
	};
}
}
