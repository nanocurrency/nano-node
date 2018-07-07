#pragma once

#include <api-c/accounts.pb.h>
#include <api-c/core.pb.h>
#include <api-c/util.pb.h>
#include <memory>
#include <rai/lib/errors.hpp>

// Convenience aliases
template <class T>
using maybe_unique_ptr = expected<std::unique_ptr<T>, std::error_code>;
template <class T>
using maybe_shared_ptr = expected<std::unique_ptr<T>, std::error_code>;

namespace rai
{
class node;
}

namespace nano
{
/** API handler errors. Do not change or reuse enum values as these propagate to clients. */
enum class error_api
{
	generic = 1,
	bad_threshold_number = 2,
	control_disabled = 3,
	unsupport_message = 4,
	invalid_count_limit = 5,
	invalid_offset = 6,
	invalid_sources_number = 7,
	invalid_starting_account = 8,
	invalid_destinations_number = 9
};

namespace api
{
	/**
 	 * Implements the Node API actions
 	 */
	class api_handler
	{
	public:
		api_handler (rai::node & node_a);

		auto parse (nano::api::RequestType query_type_a, std::vector<uint8_t> buffer_a) -> maybe_unique_ptr<google::protobuf::Message>;

		// core messages
		auto request (req_ping request) -> maybe_unique_ptr<nano::api::res_ping>;
		auto request (req_account_pending request) -> maybe_unique_ptr<res_account_pending>;

		// util messages
		auto request (req_address_valid request) -> maybe_unique_ptr<nano::api::res_address_valid>;

	private:
		rai::node & node;
		template <typename REQUEST_TYPE>
		decltype (auto) parse_and_request (std::vector<uint8_t> buffer_a);
	};
}
}

REGISTER_ERROR_CODES (nano, error_api)
