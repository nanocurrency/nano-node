#pragma once

#include <nano/node/common.hpp>
#include <nano/node/messages.hpp>
#include <nano/node/socket.hpp>

#include <boost/system/error_code.hpp>

#include <memory>
#include <vector>

namespace nano
{
class socket;

namespace transport
{
	class message_deserializer : public std::enable_shared_from_this<nano::transport::message_deserializer>
	{
	public:
		enum class parse_status
		{
			none,
			success,
			insufficient_work,
			invalid_header,
			invalid_message_type,
			invalid_keepalive_message,
			invalid_publish_message,
			invalid_confirm_req_message,
			invalid_confirm_ack_message,
			invalid_node_id_handshake_message,
			invalid_telemetry_req_message,
			invalid_telemetry_ack_message,
			invalid_bulk_pull_message,
			invalid_bulk_pull_account_message,
			invalid_frontier_req_message,
			invalid_network,
			outdated_version,
			duplicate_publish_message,
			message_size_too_big,
		};

		using callback_type = std::function<void (boost::system::error_code, std::unique_ptr<nano::message>)>;

		parse_status status;

		message_deserializer (network_constants const &, network_filter &, block_uniquer &, vote_uniquer &);

		/*
		 * Asynchronously read next message from socket.
		 * If an irrecoverable error is encountered callback will be called with an error code set and null message.
		 * If a 'soft' error is encountered (eg. duplicate block publish) error won't be set but message will be null. In that case, `status` field will be set to code indicating reason for failure.
		 * If message is received successfully, error code won't be set and message will be non-null. `status` field will be set to `success`.
		 * Should not be called until the previous invocation finishes and calls the callback.
		 */
		void read (std::shared_ptr<nano::socket> socket, callback_type const && callback);

	private:
		void received_header (std::shared_ptr<nano::socket> socket, callback_type const && callback);
		void received_message (nano::message_header header, std::size_t payload_size, callback_type const && callback);

		/*
		 * Deserializes message using data in `read_buffer`.
		 * @return If successful returns non-null message, otherwise sets `status` to error appropriate code and returns nullptr
		 */
		std::unique_ptr<nano::message> deserialize (nano::message_header header, std::size_t payload_size);
		std::unique_ptr<nano::keepalive> deserialize_keepalive (nano::stream &, nano::message_header const &);
		std::unique_ptr<nano::publish> deserialize_publish (nano::stream &, nano::message_header const &, nano::uint128_t const & = 0);
		std::unique_ptr<nano::confirm_req> deserialize_confirm_req (nano::stream &, nano::message_header const &);
		std::unique_ptr<nano::confirm_ack> deserialize_confirm_ack (nano::stream &, nano::message_header const &);
		std::unique_ptr<nano::node_id_handshake> deserialize_node_id_handshake (nano::stream &, nano::message_header const &);
		std::unique_ptr<nano::telemetry_req> deserialize_telemetry_req (nano::stream &, nano::message_header const &);
		std::unique_ptr<nano::telemetry_ack> deserialize_telemetry_ack (nano::stream &, nano::message_header const &);
		std::unique_ptr<nano::bulk_pull> deserialize_bulk_pull (nano::stream &, nano::message_header const &);
		std::unique_ptr<nano::bulk_pull_account> deserialize_bulk_pull_account (nano::stream &, nano::message_header const &);
		std::unique_ptr<nano::bulk_push> deserialize_bulk_push (nano::stream &, nano::message_header const &);
		std::unique_ptr<nano::frontier_req> deserialize_frontier_req (nano::stream &, nano::message_header const &);

		static bool at_end (nano::stream &);

		std::shared_ptr<std::vector<uint8_t>> read_buffer;

	public:
		std::string parse_status_to_string ();
		stat::detail parse_status_to_stat_detail ();

	private: // Constants
		static constexpr std::size_t HEADER_SIZE = 8;
		static constexpr std::size_t MAX_MESSAGE_SIZE = 1024 * 4;

	private: // Dependencies
		nano::network_constants const & network_constants_m;
		nano::network_filter & publish_filter_m;
		nano::block_uniquer & block_uniquer_m;
		nano::vote_uniquer & vote_uniquer_m;
	};
}
}
