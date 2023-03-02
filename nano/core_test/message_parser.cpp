#include <nano/node/transport/message_deserializer.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <vector>

template <class message_type>
auto message_deserializer_success_checker (message_type & message_original) -> void
{
	// Dependencies for the message deserializer.
	nano::network_filter filter (1);
	nano::block_uniquer block_uniquer;
	nano::vote_uniquer vote_uniquer (block_uniquer);

	// Data used to simulate the incoming buffer to be deserialized, the offset tracks how much has been read from the input_source
	// as the read function is called first to read the header, then called again to read the payload.
	std::vector<uint8_t> input_source;
	auto offset = 0u;

	// Message Deserializer with the query function tweaked to read from the `input_source`.
	auto const message_deserializer = std::make_shared<nano::transport::message_deserializer> (nano::dev::network_params.network, filter, block_uniquer, vote_uniquer,
	[&input_source, &offset] (std::shared_ptr<std::vector<uint8_t>> const & data_a, size_t size_a, std::function<void (boost::system::error_code const &, std::size_t)> callback_a) {
		debug_assert (input_source.size () >= size_a);
		data_a->resize (size_a);
		auto const copy_start = input_source.begin () + offset;
		std::copy (copy_start, copy_start + size_a, data_a->data ());
		offset += size_a;
		callback_a (boost::system::errc::make_error_code (boost::system::errc::success), size_a);
	});

	// Generating the values for the `input_source`.
	{
		nano::vectorstream stream (input_source);
		message_original.serialize (stream);
	}

	// Deserializing and testing the success path.
	message_deserializer->read (
	[&message_original] (boost::system::error_code ec_a, std::unique_ptr<nano::message> message_a) {
		auto deserialized_message = dynamic_cast<message_type *> (message_a.get ());
		ASSERT_NE (deserialized_message, nullptr);
		auto deserialized_bytes = deserialized_message->to_bytes ();
		auto original_bytes = message_original.to_bytes ();
		ASSERT_EQ (*deserialized_bytes, *original_bytes);
	});
	ASSERT_EQ (message_deserializer->status, nano::transport::message_deserializer::parse_status::success);
}

TEST (message_deserializer, exact_confirm_ack)
{
	nano::test::system system{ 1 };
	nano::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (1)
				 .destination (1)
				 .balance (2)
				 .sign (nano::keypair ().prv, 4)
				 .work (*system.work.generate (nano::root (1)))
				 .build_shared ();
	auto vote (std::make_shared<nano::vote> (0, nano::keypair ().prv, 0, 0, std::vector<nano::block_hash>{ block->hash () }));
	nano::confirm_ack message{ nano::dev::network_params.network, vote };

	message_deserializer_success_checker<decltype (message)> (message);
}

TEST (message_deserializer, exact_confirm_req)
{
	nano::test::system system{ 1 };
	nano::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (1)
				 .destination (1)
				 .balance (2)
				 .sign (nano::keypair ().prv, 4)
				 .work (*system.work.generate (nano::root (1)))
				 .build_shared ();
	nano::confirm_req message{ nano::dev::network_params.network, block };

	message_deserializer_success_checker<decltype (message)> (message);
}

TEST (message_deserializer, exact_confirm_req_hash)
{
	nano::test::system system{ 1 };
	nano::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (1)
				 .destination (1)
				 .balance (2)
				 .sign (nano::keypair ().prv, 4)
				 .work (*system.work.generate (nano::root (1)))
				 .build ();
	// This test differs from the previous `exact_confirm_req` because this tests the confirm_req created from the block hash.
	nano::confirm_req message{ nano::dev::network_params.network, block->hash (), block->root () };

	message_deserializer_success_checker<decltype (message)> (message);
}

TEST (message_deserializer, exact_publish)
{
	nano::test::system system{ 1 };
	nano::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (1)
				 .destination (1)
				 .balance (2)
				 .sign (nano::keypair ().prv, 4)
				 .work (*system.work.generate (nano::root (1)))
				 .build_shared ();
	nano::publish message{ nano::dev::network_params.network, block };

	message_deserializer_success_checker<decltype (message)> (message);
}

TEST (message_deserializer, exact_keepalive)
{
	nano::keepalive message{ nano::dev::network_params.network };

	message_deserializer_success_checker<decltype (message)> (message);
}
