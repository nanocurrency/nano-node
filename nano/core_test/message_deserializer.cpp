#include <celerix/lib/blocks.hpp>
#include <celerix/node/transport/message_deserializer.hpp>
#include <celerix/secure/vote.hpp>
#include <celerix/test_common/system.hpp>
#include <celerix/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <vector>

// Test the successful cases for message_deserializer, checking the supported message types and
// the integrity of the deserialized outcome.
template <class message_type>
auto message_deserializer_success_checker (message_type & message_original) -> void
{
	// Dependencies for the message deserializer.
	celerix::network_filter filter (1);
	celerix::block_uniquer block_uniquer;
	celerix::vote_uniquer vote_uniquer;

	// Data used to simulate the incoming buffer to be deserialized, the offset tracks how much has been read from the input_source
	// as the read function is called first to read the header, then called again to read the payload.
	std::vector<uint8_t> input_source;
	std::size_t offset{ 0 };

	// Message Deserializer with the query function tweaked to read from the `input_source`.
	auto const message_deserializer = std::make_shared<celerix::transport::message_deserializer> (celerix::dev::network_params.network, filter, block_uniquer, vote_uniquer,
	[&input_source, &offset] (std::shared_ptr<std::vector<uint8_t>> const & data_a, std::size_t size_a, std::function<void (boost::system::error_code const &, std::size_t)> callback_a) {
		debug_assert (input_source.size () >= size_a);
		data_a->resize (size_a);
		auto const copy_start = input_source.begin () + offset;
		std::copy (copy_start, copy_start + size_a, data_a->data ());
		offset += size_a;
		callback_a (boost::system::errc::make_error_code (boost::system::errc::success), size_a);
	});

	// Generating the values for the `input_source`.
	{
		celerix::vectorstream stream (input_source);
		message_original.serialize (stream);
	}

	// Deserializing and testing the success path.
	message_deserializer->read (
	[&message_original] (boost::system::error_code ec_a, std::unique_ptr<celerix::message> message_a) {
		auto deserialized_message = dynamic_cast<message_type *> (message_a.get ());
		// Ensure the message type is supported.
		ASSERT_NE (deserialized_message, nullptr);
		auto deserialized_bytes = deserialized_message->to_bytes ();
		auto original_bytes = message_original.to_bytes ();
		// Ensure the integrity of the deserialized message.
		ASSERT_EQ (*deserialized_bytes, *original_bytes);
	});
	// This is a sanity test, to ensure the successful deserialization case passes.
	ASSERT_EQ (message_deserializer->status, celerix::transport::parse_status::success);
}

TEST (message_deserializer, exact_confirm_ack)
{
	celerix::test::system system{ 1 };
	celerix::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (1)
				 .destination (1)
				 .balance (2)
				 .sign (celerix::keypair ().prv, 4)
				 .work (*system.work.generate (celerix::root (1)))
				 .build ();
	auto vote (std::make_shared<celerix::vote> (0, celerix::keypair ().prv, 0, 0, std::vector<celerix::block_hash>{ block->hash () }));
	celerix::confirm_ack message{ celerix::dev::network_params.network, vote };

	message_deserializer_success_checker<decltype (message)> (message);
}

TEST (message_deserializer, exact_confirm_req_hash)
{
	celerix::test::system system{ 1 };
	celerix::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (1)
				 .destination (1)
				 .balance (2)
				 .sign (celerix::keypair ().prv, 4)
				 .work (*system.work.generate (celerix::root (1)))
				 .build ();
	// This test differs from the previous `exact_confirm_req` because this tests the confirm_req created from the block hash.
	celerix::confirm_req message{ celerix::dev::network_params.network, block->hash (), block->root () };

	message_deserializer_success_checker<decltype (message)> (message);
}

TEST (message_deserializer, exact_publish)
{
	celerix::test::system system{ 1 };
	celerix::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (1)
				 .destination (1)
				 .balance (2)
				 .sign (celerix::keypair ().prv, 4)
				 .work (*system.work.generate (celerix::root (1)))
				 .build ();
	celerix::publish message{ celerix::dev::network_params.network, block };

	message_deserializer_success_checker<decltype (message)> (message);
}

TEST (message_deserializer, exact_keepalive)
{
	celerix::keepalive message{ celerix::dev::network_params.network };

	message_deserializer_success_checker<decltype (message)> (message);
}

TEST (message_deserializer, exact_frontier_req)
{
	celerix::frontier_req message{ celerix::dev::network_params.network };
	message_deserializer_success_checker<decltype (message)> (message);
}

TEST (message_deserializer, exact_telemetry_req)
{
	celerix::telemetry_req message{ celerix::dev::network_params.network };
	message_deserializer_success_checker<decltype (message)> (message);
}

TEST (message_deserializer, exact_telemetry_ack)
{
	celerix::telemetry_data data;
	data.unknown_data.push_back (0xFF);

	celerix::telemetry_ack message{ celerix::dev::network_params.network, data };
	message_deserializer_success_checker<decltype (message)> (message);
}

TEST (message_deserializer, exact_bulk_pull)
{
	celerix::bulk_pull message{ celerix::dev::network_params.network };
	message.header.flag_set (celerix::message_header::bulk_pull_ascending_flag);

	message_deserializer_success_checker<decltype (message)> (message);
}

TEST (message_deserializer, exact_bulk_pull_account)
{
	celerix::bulk_pull_account message{ celerix::dev::network_params.network };
	message.flags = celerix::bulk_pull_account_flags::pending_address_only;

	message_deserializer_success_checker<decltype (message)> (message);
}

TEST (message_deserializer, exact_bulk_push)
{
	celerix::bulk_push message{ celerix::dev::network_params.network };
	message_deserializer_success_checker<decltype (message)> (message);
}

TEST (message_deserializer, exact_node_id_handshake)
{
	celerix::node_id_handshake message{ celerix::dev::network_params.network, std::nullopt, std::nullopt };
	message_deserializer_success_checker<decltype (message)> (message);
}

TEST (message_deserializer, exact_asc_pull_req)
{
	celerix::asc_pull_req message{ celerix::dev::network_params.network };

	// The asc_pull_req checks for the message fields and the payload to be filled.
	message.id = 7;
	message.type = celerix::asc_pull_type::account_info;

	celerix::asc_pull_req::account_info_payload message_payload;
	message_payload.target = celerix::test::random_account ();
	message_payload.target_type = celerix::asc_pull_req::hash_type::account;

	message.payload = message_payload;
	message.update_header ();

	message_deserializer_success_checker<decltype (message)> (message);
}

TEST (message_deserializer, exact_asc_pull_ack)
{
	celerix::asc_pull_ack message{ celerix::dev::network_params.network };

	// The asc_pull_ack checks for the message fields and the payload to be filled.
	message.id = 11;
	message.type = celerix::asc_pull_type::account_info;

	celerix::asc_pull_ack::account_info_payload message_payload;
	message_payload.account = celerix::test::random_account ();
	message_payload.account_open = celerix::test::random_hash ();
	message_payload.account_head = celerix::test::random_hash ();
	message_payload.account_block_count = 932932132;
	message_payload.account_conf_frontier = celerix::test::random_hash ();
	message_payload.account_conf_height = 847312;

	message.payload = message_payload;
	message.update_header ();

	message_deserializer_success_checker<decltype (message)> (message);
}
