#include <nano/crypto_lib/random_pool.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/stream.hpp>
#include <nano/node/common.hpp>
#include <nano/node/network.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/asio/ip/address_v6.hpp>

namespace
{
std::shared_ptr<nano::block> random_block ()
{
	nano::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (nano::test::random_hash ())
				 .destination (nano::keypair ().pub)
				 .balance (2)
				 .sign (nano::keypair ().prv, 4)
				 .work (5)
				 .build ();
	return block;
}
}

TEST (message, header_version)
{
	// Simplest message type
	nano::keepalive original{ nano::dev::network_params.network };

	// Serialize the original keepalive message
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream (bytes);
		original.serialize (stream);
	}

	// Deserialize the byte stream back to a message header
	nano::bufferstream stream (bytes.data (), bytes.size ());
	bool error = false;
	nano::message_header header (error, stream);
	ASSERT_FALSE (error);

	// Check header versions
	ASSERT_EQ (nano::dev::network_params.network.protocol_version_min, header.version_min);
	ASSERT_EQ (nano::dev::network_params.network.protocol_version, header.version_using);
	ASSERT_EQ (nano::dev::network_params.network.protocol_version, header.version_max);
	ASSERT_EQ (nano::message_type::keepalive, header.type);
}

TEST (message, keepalive_serialization)
{
	nano::keepalive request1{ nano::dev::network_params.network };
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream (bytes);
		request1.serialize (stream);
	}
	auto error (false);
	nano::bufferstream stream (bytes.data (), bytes.size ());
	nano::message_header header (error, stream);
	ASSERT_FALSE (error);
	nano::keepalive request2 (error, stream, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (request1, request2);
}

TEST (message, keepalive_deserialize)
{
	nano::keepalive message1{ nano::dev::network_params.network };
	message1.peers[0] = nano::endpoint (boost::asio::ip::address_v6::loopback (), 10000);
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream (bytes);
		message1.serialize (stream);
	}
	nano::bufferstream stream (bytes.data (), bytes.size ());
	auto error (false);
	nano::message_header header (error, stream);
	ASSERT_FALSE (error);
	ASSERT_EQ (nano::message_type::keepalive, header.type);
	nano::keepalive message2 (error, stream, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (message1.peers, message2.peers);
}

TEST (message, publish)
{
	// Create a random block
	auto block = random_block ();
	nano::publish original{ nano::dev::network_params.network, block };
	ASSERT_FALSE (original.is_originator ());

	// Serialize the original publish message
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream (bytes);
		original.serialize (stream);
	}

	// Deserialize the byte stream back to a publish message
	nano::bufferstream stream (bytes.data (), bytes.size ());
	bool error = false;
	nano::message_header header (error, stream);
	ASSERT_FALSE (error);
	nano::publish deserialized (error, stream, header);
	ASSERT_FALSE (error);

	// Assert that the original and deserialized messages are equal
	ASSERT_EQ (original, deserialized);
	ASSERT_EQ (*original.block, *deserialized.block);
	ASSERT_EQ (original.is_originator (), deserialized.is_originator ());
}

TEST (message, publish_originator_flag)
{
	// Create a random block
	auto block = random_block ();
	nano::publish original{ nano::dev::network_params.network, block, /* originator */ true };
	ASSERT_TRUE (original.is_originator ());

	// Serialize the original publish message
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream (bytes);
		original.serialize (stream);
	}

	// Deserialize the byte stream back to a publish message
	nano::bufferstream stream (bytes.data (), bytes.size ());
	bool error = false;
	nano::message_header header (error, stream);
	ASSERT_FALSE (error);
	nano::publish deserialized (error, stream, header);
	ASSERT_FALSE (error);

	// Assert that the originator flag is set correctly in both the original and deserialized messages
	ASSERT_TRUE (deserialized.is_originator ());
	ASSERT_EQ (original, deserialized);
	ASSERT_EQ (*original.block, *deserialized.block);
}

TEST (message, confirm_header_flags)
{
	nano::message_header header_v2{ nano::dev::network_params.network, nano::message_type::confirm_req };
	header_v2.confirm_set_v2 (true);

	const uint8_t value = 0b0110'1001;

	header_v2.count_v2_set (value); // Max count value

	ASSERT_TRUE (header_v2.confirm_is_v2 ());
	ASSERT_EQ (header_v2.count_v2_get (), value);

	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream (bytes);
		header_v2.serialize (stream);
	}
	nano::bufferstream stream (bytes.data (), bytes.size ());

	bool error = false;
	nano::message_header header (error, stream);
	ASSERT_FALSE (error);
	ASSERT_EQ (nano::message_type::confirm_req, header.type);

	ASSERT_TRUE (header.confirm_is_v2 ());
	ASSERT_EQ (header.count_v2_get (), value);
}

TEST (message, confirm_header_flags_max)
{
	nano::message_header header_v2{ nano::dev::network_params.network, nano::message_type::confirm_req };
	header_v2.confirm_set_v2 (true);
	header_v2.count_v2_set (255); // Max count value

	ASSERT_TRUE (header_v2.confirm_is_v2 ());
	ASSERT_EQ (header_v2.count_v2_get (), 255);

	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream (bytes);
		header_v2.serialize (stream);
	}
	nano::bufferstream stream (bytes.data (), bytes.size ());

	bool error = false;
	nano::message_header header (error, stream);
	ASSERT_FALSE (error);
	ASSERT_EQ (nano::message_type::confirm_req, header.type);

	ASSERT_TRUE (header.confirm_is_v2 ());
	ASSERT_EQ (header.count_v2_get (), 255);
}

TEST (message, confirm_ack_hash_serialization)
{
	std::vector<nano::block_hash> hashes;
	for (auto i (hashes.size ()); i < 15; i++)
	{
		nano::keypair key1;
		nano::block_hash previous;
		nano::random_pool::generate_block (previous.bytes.data (), previous.bytes.size ());
		nano::block_builder builder;
		auto block = builder
					 .state ()
					 .account (key1.pub)
					 .previous (previous)
					 .representative (key1.pub)
					 .balance (2)
					 .link (4)
					 .sign (key1.prv, key1.pub)
					 .work (5)
					 .build ();
		hashes.push_back (block->hash ());
	}
	nano::keypair representative1;
	auto vote = nano::test::make_vote (representative1, { hashes }, 0, 0);
	nano::confirm_ack con1{ nano::dev::network_params.network, vote };
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream1 (bytes);
		con1.serialize (stream1);
	}
	nano::bufferstream stream2 (bytes.data (), bytes.size ());
	bool error (false);
	nano::message_header header (error, stream2);
	nano::confirm_ack con2 (error, stream2, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (con1, con2);
	ASSERT_EQ (hashes, con2.vote->hashes);
	ASSERT_FALSE (header.confirm_is_v2 ());
	ASSERT_EQ (header.count_get (), hashes.size ());
	ASSERT_FALSE (con2.is_rebroadcasted ());
}

TEST (message, confirm_ack_hash_serialization_v2)
{
	std::vector<nano::block_hash> hashes;
	for (auto i (hashes.size ()); i < 255; i++)
	{
		nano::keypair key1;
		nano::block_hash previous;
		nano::random_pool::generate_block (previous.bytes.data (), previous.bytes.size ());
		nano::block_builder builder;
		auto block = builder
					 .state ()
					 .account (key1.pub)
					 .previous (previous)
					 .representative (key1.pub)
					 .balance (2)
					 .link (4)
					 .sign (key1.prv, key1.pub)
					 .work (5)
					 .build ();
		hashes.push_back (block->hash ());
	}

	nano::keypair representative1;
	auto vote = nano::test::make_vote (representative1, { hashes }, 0, 0);
	nano::confirm_ack con1{ nano::dev::network_params.network, vote };
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream1 (bytes);
		con1.serialize (stream1);
	}
	nano::bufferstream stream2 (bytes.data (), bytes.size ());
	bool error (false);
	nano::message_header header (error, stream2);
	nano::confirm_ack con2 (error, stream2, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (con1, con2);
	ASSERT_EQ (hashes, con2.vote->hashes);
	ASSERT_TRUE (header.confirm_is_v2 ());
	ASSERT_EQ (header.count_v2_get (), hashes.size ());
	ASSERT_FALSE (con2.is_rebroadcasted ());
}

TEST (message, confirm_ack_rebroadcasted_flag)
{
	nano::keypair representative1;
	auto vote = nano::test::make_vote (representative1, std::vector<nano::block_hash> (), 0, 0);
	nano::confirm_ack con1{ nano::dev::network_params.network, vote, /* rebroadcasted */ true };
	ASSERT_TRUE (con1.is_rebroadcasted ());
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream1 (bytes);
		con1.serialize (stream1);
	}
	nano::bufferstream stream2 (bytes.data (), bytes.size ());
	bool error (false);
	nano::message_header header (error, stream2);
	nano::confirm_ack con2 (error, stream2, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (con1, con2);
	ASSERT_TRUE (con2.vote->hashes.empty ());
	ASSERT_TRUE (con2.is_rebroadcasted ());
}

TEST (message, confirm_req_hash_serialization)
{
	nano::keypair key1;
	nano::keypair key2;
	nano::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (1)
				 .destination (key2.pub)
				 .balance (200)
				 .sign (nano::keypair ().prv, 2)
				 .work (3)
				 .build ();
	nano::confirm_req req{ nano::dev::network_params.network, block->hash (), block->root () };
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream (bytes);
		req.serialize (stream);
	}
	auto error (false);
	nano::bufferstream stream2 (bytes.data (), bytes.size ());
	nano::message_header header (error, stream2);
	nano::confirm_req req2 (error, stream2, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (req, req2);
	ASSERT_EQ (req.roots_hashes, req2.roots_hashes);
	ASSERT_EQ (header.count_get (), req.roots_hashes.size ());
}

TEST (message, confirm_req_hash_batch_serialization)
{
	nano::keypair key;
	nano::keypair representative;
	std::vector<std::pair<nano::block_hash, nano::root>> roots_hashes;
	nano::block_builder builder;
	auto open = builder
				.state ()
				.account (key.pub)
				.previous (0)
				.representative (representative.pub)
				.balance (2)
				.link (4)
				.sign (key.prv, key.pub)
				.work (5)
				.build ();
	roots_hashes.push_back (std::make_pair (open->hash (), open->root ()));
	for (auto i (roots_hashes.size ()); i < 7; i++)
	{
		nano::keypair key1;
		nano::block_hash previous;
		nano::random_pool::generate_block (previous.bytes.data (), previous.bytes.size ());
		auto block = builder
					 .state ()
					 .account (key1.pub)
					 .previous (previous)
					 .representative (representative.pub)
					 .balance (2)
					 .link (4)
					 .sign (key1.prv, key1.pub)
					 .work (5)
					 .build ();
		roots_hashes.push_back (std::make_pair (block->hash (), block->root ()));
	}
	roots_hashes.push_back (std::make_pair (open->hash (), open->root ()));
	nano::confirm_req req{ nano::dev::network_params.network, roots_hashes };
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream (bytes);
		req.serialize (stream);
	}
	auto error (false);
	nano::bufferstream stream2 (bytes.data (), bytes.size ());
	nano::message_header header (error, stream2);
	nano::confirm_req req2 (error, stream2, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (req, req2);
	ASSERT_EQ (req.roots_hashes, req2.roots_hashes);
	ASSERT_EQ (req.roots_hashes, roots_hashes);
	ASSERT_EQ (req2.roots_hashes, roots_hashes);
	ASSERT_EQ (header.count_get (), req.roots_hashes.size ());
	ASSERT_FALSE (header.confirm_is_v2 ());
}

TEST (message, confirm_req_hash_batch_serialization_v2)
{
	nano::keypair key;
	nano::keypair representative;
	nano::block_builder builder;
	auto open = builder
				.state ()
				.account (key.pub)
				.previous (0)
				.representative (representative.pub)
				.balance (2)
				.link (4)
				.sign (key.prv, key.pub)
				.work (5)
				.build ();

	std::vector<std::pair<nano::block_hash, nano::root>> roots_hashes;
	roots_hashes.push_back (std::make_pair (open->hash (), open->root ()));
	for (auto i (roots_hashes.size ()); i < 255; i++)
	{
		nano::keypair key1;
		nano::block_hash previous;
		nano::random_pool::generate_block (previous.bytes.data (), previous.bytes.size ());
		auto block = builder
					 .state ()
					 .account (key1.pub)
					 .previous (previous)
					 .representative (representative.pub)
					 .balance (2)
					 .link (4)
					 .sign (key1.prv, key1.pub)
					 .work (5)
					 .build ();
		roots_hashes.push_back (std::make_pair (block->hash (), block->root ()));
	}

	nano::confirm_req req{ nano::dev::network_params.network, roots_hashes };
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream (bytes);
		req.serialize (stream);
	}
	auto error (false);
	nano::bufferstream stream2 (bytes.data (), bytes.size ());
	nano::message_header header (error, stream2);
	nano::confirm_req req2 (error, stream2, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (req, req2);
	ASSERT_EQ (req.roots_hashes, req2.roots_hashes);
	ASSERT_EQ (req.roots_hashes, roots_hashes);
	ASSERT_EQ (req2.roots_hashes, roots_hashes);
	ASSERT_EQ (header.count_v2_get (), req.roots_hashes.size ());
	ASSERT_TRUE (header.confirm_is_v2 ());
}

/**
 * Test that a confirm_ack can encode an empty hash set
 */
TEST (confirm_ack, empty_vote_hashes)
{
	nano::keypair key;
	auto vote = std::make_shared<nano::vote> (key.pub, key.prv, 0, 0, std::vector<nano::block_hash>{} /* empty */);
	nano::confirm_ack message{ nano::dev::network_params.network, vote };
}

TEST (message, bulk_pull_serialization)
{
	nano::bulk_pull message_in{ nano::dev::network_params.network };
	message_in.header.flag_set (nano::message_header::bulk_pull_ascending_flag);
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream{ bytes };
		message_in.serialize (stream);
	}
	nano::bufferstream stream{ bytes.data (), bytes.size () };
	bool error = false;
	nano::message_header header{ error, stream };
	ASSERT_FALSE (error);
	nano::bulk_pull message_out{ error, stream, header };
	ASSERT_FALSE (error);
	ASSERT_TRUE (header.bulk_pull_ascending ());
}

TEST (message, asc_pull_req_serialization_blocks)
{
	nano::asc_pull_req original{ nano::dev::network_params.network };
	original.id = 7;
	original.type = nano::asc_pull_type::blocks;

	nano::asc_pull_req::blocks_payload original_payload{};
	original_payload.start = nano::test::random_hash ();
	original_payload.count = 111;

	original.payload = original_payload;
	original.update_header ();

	// Serialize
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream{ bytes };
		original.serialize (stream);
	}
	nano::bufferstream stream{ bytes.data (), bytes.size () };

	// Header
	bool error = false;
	nano::message_header header (error, stream);
	ASSERT_FALSE (error);
	ASSERT_EQ (nano::message_type::asc_pull_req, header.type);

	// Message
	nano::asc_pull_req message (error, stream, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (original.id, message.id);
	ASSERT_EQ (original.type, message.type);

	nano::asc_pull_req::blocks_payload message_payload;
	ASSERT_NO_THROW (message_payload = std::get<nano::asc_pull_req::blocks_payload> (message.payload));
	ASSERT_EQ (original_payload.start, message_payload.start);
	ASSERT_EQ (original_payload.count, message_payload.count);

	ASSERT_TRUE (nano::at_end (stream));
}

TEST (message, asc_pull_req_serialization_account_info)
{
	nano::asc_pull_req original{ nano::dev::network_params.network };
	original.id = 7;
	original.type = nano::asc_pull_type::account_info;

	nano::asc_pull_req::account_info_payload original_payload{};
	original_payload.target = nano::test::random_hash ();

	original.payload = original_payload;
	original.update_header ();

	// Serialize
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream{ bytes };
		original.serialize (stream);
	}
	nano::bufferstream stream{ bytes.data (), bytes.size () };

	// Header
	bool error = false;
	nano::message_header header (error, stream);
	ASSERT_FALSE (error);
	ASSERT_EQ (nano::message_type::asc_pull_req, header.type);

	// Message
	nano::asc_pull_req message (error, stream, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (original.id, message.id);
	ASSERT_EQ (original.type, message.type);

	nano::asc_pull_req::account_info_payload message_payload;
	ASSERT_NO_THROW (message_payload = std::get<nano::asc_pull_req::account_info_payload> (message.payload));
	ASSERT_EQ (original_payload.target, message_payload.target);

	ASSERT_TRUE (nano::at_end (stream));
}

TEST (message, asc_pull_req_serialization_frontiers)
{
	nano::asc_pull_req original{ nano::dev::network_params.network };
	original.id = 7;
	original.type = nano::asc_pull_type::frontiers;

	nano::asc_pull_req::frontiers_payload original_payload{};
	original_payload.start = nano::test::random_account ();
	original_payload.count = 123;

	original.payload = original_payload;
	original.update_header ();

	// Serialize
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream{ bytes };
		original.serialize (stream);
	}
	nano::bufferstream stream{ bytes.data (), bytes.size () };

	// Header
	bool error = false;
	nano::message_header header (error, stream);
	ASSERT_FALSE (error);
	ASSERT_EQ (nano::message_type::asc_pull_req, header.type);

	// Message
	nano::asc_pull_req message (error, stream, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (original.id, message.id);
	ASSERT_EQ (original.type, message.type);

	nano::asc_pull_req::frontiers_payload message_payload;
	ASSERT_NO_THROW (message_payload = std::get<nano::asc_pull_req::frontiers_payload> (message.payload));
	ASSERT_EQ (original_payload.start, message_payload.start);
	ASSERT_EQ (original_payload.count, message_payload.count);

	ASSERT_TRUE (nano::at_end (stream));
}

TEST (message, asc_pull_ack_serialization_blocks)
{
	nano::asc_pull_ack original{ nano::dev::network_params.network };
	original.id = 11;
	original.type = nano::asc_pull_type::blocks;

	nano::asc_pull_ack::blocks_payload original_payload{};
	for (int n = 0; n < nano::asc_pull_ack::blocks_payload::max_blocks; ++n)
	{
		original_payload.blocks.push_back (random_block ());
	}

	original.payload = original_payload;
	original.update_header ();

	// Serialize
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream{ bytes };
		original.serialize (stream);
	}
	nano::bufferstream stream{ bytes.data (), bytes.size () };

	// Header
	bool error = false;
	nano::message_header header (error, stream);
	ASSERT_FALSE (error);
	ASSERT_EQ (nano::message_type::asc_pull_ack, header.type);

	// Message
	nano::asc_pull_ack message (error, stream, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (original.id, message.id);
	ASSERT_EQ (original.type, message.type);

	nano::asc_pull_ack::blocks_payload message_payload;
	ASSERT_NO_THROW (message_payload = std::get<nano::asc_pull_ack::blocks_payload> (message.payload));

	// Compare blocks
	ASSERT_EQ (original_payload.blocks.size (), message_payload.blocks.size ());
	ASSERT_TRUE (std::equal (original_payload.blocks.begin (), original_payload.blocks.end (), message_payload.blocks.begin (), message_payload.blocks.end (), [] (auto a, auto b) {
		return *a == *b;
	}));

	ASSERT_TRUE (nano::at_end (stream));
}

TEST (message, asc_pull_ack_serialization_account_info)
{
	nano::asc_pull_ack original{ nano::dev::network_params.network };
	original.id = 11;
	original.type = nano::asc_pull_type::account_info;

	nano::asc_pull_ack::account_info_payload original_payload{};
	original_payload.account = nano::test::random_account ();
	original_payload.account_open = nano::test::random_hash ();
	original_payload.account_head = nano::test::random_hash ();
	original_payload.account_block_count = 932932132;
	original_payload.account_conf_frontier = nano::test::random_hash ();
	original_payload.account_conf_height = 847312;

	original.payload = original_payload;
	original.update_header ();

	// Serialize
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream{ bytes };
		original.serialize (stream);
	}
	nano::bufferstream stream{ bytes.data (), bytes.size () };

	// Header
	bool error = false;
	nano::message_header header (error, stream);
	ASSERT_FALSE (error);
	ASSERT_EQ (nano::message_type::asc_pull_ack, header.type);

	// Message
	nano::asc_pull_ack message (error, stream, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (original.id, message.id);
	ASSERT_EQ (original.type, message.type);

	nano::asc_pull_ack::account_info_payload message_payload;
	ASSERT_NO_THROW (message_payload = std::get<nano::asc_pull_ack::account_info_payload> (message.payload));

	ASSERT_EQ (original_payload.account, message_payload.account);
	ASSERT_EQ (original_payload.account_open, message_payload.account_open);
	ASSERT_EQ (original_payload.account_head, message_payload.account_head);
	ASSERT_EQ (original_payload.account_block_count, message_payload.account_block_count);
	ASSERT_EQ (original_payload.account_conf_frontier, message_payload.account_conf_frontier);
	ASSERT_EQ (original_payload.account_conf_height, message_payload.account_conf_height);

	ASSERT_TRUE (nano::at_end (stream));
}

TEST (message, asc_pull_ack_serialization_frontiers)
{
	nano::asc_pull_ack original{ nano::dev::network_params.network };
	original.id = 11;
	original.type = nano::asc_pull_type::frontiers;

	nano::asc_pull_ack::frontiers_payload original_payload{};
	for (int n = 0; n < nano::asc_pull_ack::frontiers_payload::max_frontiers; ++n)
	{
		original_payload.frontiers.push_back ({ nano::test::random_account (), nano::test::random_hash () });
	}

	original.payload = original_payload;
	original.update_header ();

	// Serialize
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream{ bytes };
		original.serialize (stream);
	}
	nano::bufferstream stream{ bytes.data (), bytes.size () };

	// Header
	bool error = false;
	nano::message_header header (error, stream);
	ASSERT_FALSE (error);
	ASSERT_EQ (nano::message_type::asc_pull_ack, header.type);

	// Message
	nano::asc_pull_ack message (error, stream, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (original.id, message.id);
	ASSERT_EQ (original.type, message.type);

	nano::asc_pull_ack::frontiers_payload message_payload;
	ASSERT_NO_THROW (message_payload = std::get<nano::asc_pull_ack::frontiers_payload> (message.payload));

	ASSERT_EQ (original_payload.frontiers, message_payload.frontiers);

	ASSERT_TRUE (nano::at_end (stream));
}

TEST (message, node_id_handshake_query_serialization)
{
	nano::node_id_handshake::query_payload query{};
	query.cookie = 7;
	nano::node_id_handshake original{ nano::dev::network_params.network, query };

	// Serialize
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream{ bytes };
		original.serialize (stream);
	}
	nano::bufferstream stream{ bytes.data (), bytes.size () };

	// Header
	bool error = false;
	nano::message_header header (error, stream);
	ASSERT_FALSE (error);
	ASSERT_EQ (nano::message_type::node_id_handshake, header.type);

	// Message
	nano::node_id_handshake message{ error, stream, header };
	ASSERT_FALSE (error);
	ASSERT_TRUE (message.query);
	ASSERT_FALSE (message.response);

	ASSERT_EQ (original.query->cookie, message.query->cookie);

	ASSERT_TRUE (nano::at_end (stream));
}

TEST (message, node_id_handshake_response_serialization)
{
	nano::node_id_handshake::response_payload response{};
	response.node_id = nano::account{ 7 };
	response.signature = nano::signature{ 11 };
	nano::node_id_handshake original{ nano::dev::network_params.network, std::nullopt, response };

	// Serialize
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream{ bytes };
		original.serialize (stream);
	}
	nano::bufferstream stream{ bytes.data (), bytes.size () };

	// Header
	bool error = false;
	nano::message_header header (error, stream);
	ASSERT_FALSE (error);
	ASSERT_EQ (nano::message_type::node_id_handshake, header.type);

	// Message
	nano::node_id_handshake message{ error, stream, header };
	ASSERT_FALSE (error);
	ASSERT_FALSE (message.query);
	ASSERT_TRUE (message.response);
	ASSERT_FALSE (message.response->v2);

	ASSERT_EQ (original.response->node_id, message.response->node_id);
	ASSERT_EQ (original.response->signature, message.response->signature);

	ASSERT_TRUE (nano::at_end (stream));
}

TEST (message, node_id_handshake_response_v2_serialization)
{
	nano::node_id_handshake::response_payload response{};
	response.node_id = nano::account{ 7 };
	response.signature = nano::signature{ 11 };
	nano::node_id_handshake::response_payload::v2_payload v2_pld{};
	v2_pld.salt = 17;
	v2_pld.genesis = nano::block_hash{ 13 };
	response.v2 = v2_pld;

	nano::node_id_handshake original{ nano::dev::network_params.network, std::nullopt, response };

	// Serialize
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream{ bytes };
		original.serialize (stream);
	}
	nano::bufferstream stream{ bytes.data (), bytes.size () };

	// Header
	bool error = false;
	nano::message_header header (error, stream);
	ASSERT_FALSE (error);
	ASSERT_EQ (nano::message_type::node_id_handshake, header.type);

	// Message
	nano::node_id_handshake message{ error, stream, header };
	ASSERT_FALSE (error);
	ASSERT_FALSE (message.query);
	ASSERT_TRUE (message.response);
	ASSERT_TRUE (message.response->v2);

	ASSERT_EQ (original.response->node_id, message.response->node_id);
	ASSERT_EQ (original.response->signature, message.response->signature);
	ASSERT_EQ (original.response->v2->salt, message.response->v2->salt);
	ASSERT_EQ (original.response->v2->genesis, message.response->v2->genesis);

	ASSERT_TRUE (nano::at_end (stream));
}

TEST (handshake, signature)
{
	nano::keypair node_id{};
	nano::keypair node_id_2{};
	auto cookie = nano::random_pool::generate<nano::uint256_union> ();
	auto cookie_2 = nano::random_pool::generate<nano::uint256_union> ();

	nano::node_id_handshake::response_payload response{};
	response.node_id = node_id.pub;
	response.sign (cookie, node_id);
	ASSERT_TRUE (response.validate (cookie));

	// Invalid cookie
	ASSERT_FALSE (response.validate (cookie_2));

	// Invalid node id
	response.node_id = node_id_2.pub;
	ASSERT_FALSE (response.validate (cookie));
}

TEST (handshake, signature_v2)
{
	nano::keypair node_id{};
	nano::keypair node_id_2{};
	auto cookie = nano::random_pool::generate<nano::uint256_union> ();
	auto cookie_2 = nano::random_pool::generate<nano::uint256_union> ();

	nano::node_id_handshake::response_payload original{};
	original.node_id = node_id.pub;
	original.v2 = nano::node_id_handshake::response_payload::v2_payload{};
	original.v2->genesis = nano::test::random_hash ();
	original.v2->salt = nano::random_pool::generate<nano::uint256_union> ();
	original.sign (cookie, node_id);
	ASSERT_TRUE (original.validate (cookie));

	// Invalid cookie
	ASSERT_FALSE (original.validate (cookie_2));

	// Invalid node id
	{
		auto message = original;
		ASSERT_TRUE (message.validate (cookie));
		message.node_id = node_id_2.pub;
		ASSERT_FALSE (message.validate (cookie));
	}

	// Invalid genesis
	{
		auto message = original;
		ASSERT_TRUE (message.validate (cookie));
		message.v2->genesis = nano::test::random_hash ();
		ASSERT_FALSE (message.validate (cookie));
	}

	// Invalid salt
	{
		auto message = original;
		ASSERT_TRUE (message.validate (cookie));
		message.v2->salt = nano::random_pool::generate<nano::uint256_union> ();
		ASSERT_FALSE (message.validate (cookie));
	}
}
