#include <gtest/gtest.h>

#include <memory>

#include <nano/lib/blocks.hpp>
#include <nano/lib/interface.h>
#include <nano/lib/numbers.hpp>
#include <nano/lib/work.hpp>

TEST (interface, xrb_uint128_to_dec)
{
	nano::uint128_union zero (0);
	char text[40] = { 0 };
	xrb_uint128_to_dec (zero.bytes.data (), text);
	ASSERT_STREQ ("0", text);
}

TEST (interface, xrb_uint256_to_string)
{
	nano::uint256_union zero (0);
	char text[65] = { 0 };
	xrb_uint256_to_string (zero.bytes.data (), text);
	ASSERT_STREQ ("0000000000000000000000000000000000000000000000000000000000000000", text);
}

TEST (interface, xrb_uint256_to_address)
{
	nano::uint256_union zero (0);
	char text[65] = { 0 };
	xrb_uint256_to_address (zero.bytes.data (), text);
	ASSERT_STREQ ("xrb_1111111111111111111111111111111111111111111111111111hifc8npp", text);
}

TEST (interface, xrb_uint512_to_string)
{
	nano::uint512_union zero (0);
	char text[129] = { 0 };
	xrb_uint512_to_string (zero.bytes.data (), text);
	ASSERT_STREQ ("00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000", text);
}

TEST (interface, xrb_uint128_from_dec)
{
	nano::uint128_union zero (0);
	ASSERT_EQ (0, xrb_uint128_from_dec ("340282366920938463463374607431768211455", zero.bytes.data ()));
	ASSERT_EQ (1, xrb_uint128_from_dec ("340282366920938463463374607431768211456", zero.bytes.data ()));
	ASSERT_EQ (1, xrb_uint128_from_dec ("3402823669209384634633%4607431768211455", zero.bytes.data ()));
}

TEST (interface, xrb_uint256_from_string)
{
	nano::uint256_union zero (0);
	ASSERT_EQ (0, xrb_uint256_from_string ("0000000000000000000000000000000000000000000000000000000000000000", zero.bytes.data ()));
	ASSERT_EQ (1, xrb_uint256_from_string ("00000000000000000000000000000000000000000000000000000000000000000", zero.bytes.data ()));
	ASSERT_EQ (1, xrb_uint256_from_string ("000000000000000000000000000%000000000000000000000000000000000000", zero.bytes.data ()));
}

TEST (interface, xrb_uint512_from_string)
{
	nano::uint512_union zero (0);
	ASSERT_EQ (0, xrb_uint512_from_string ("00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000", zero.bytes.data ()));
	ASSERT_EQ (1, xrb_uint512_from_string ("000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000", zero.bytes.data ()));
	ASSERT_EQ (1, xrb_uint512_from_string ("0000000000000000000000000000000000000000000000000000000000%000000000000000000000000000000000000000000000000000000000000000000000", zero.bytes.data ()));
}

TEST (interface, xrb_valid_address)
{
	ASSERT_EQ (0, xrb_valid_address ("xrb_1111111111111111111111111111111111111111111111111111hifc8npp"));
	ASSERT_EQ (1, xrb_valid_address ("xrb_1111111111111111111111111111111111111111111111111111hifc8nppp"));
	ASSERT_EQ (1, xrb_valid_address ("xrb_1111111211111111111111111111111111111111111111111111hifc8npp"));
}

TEST (interface, xrb_seed_create)
{
	nano::uint256_union seed;
	xrb_generate_random (seed.bytes.data ());
	ASSERT_FALSE (seed.is_zero ());
}

TEST (interface, xrb_seed_key)
{
	nano::uint256_union seed (0);
	nano::uint256_union prv;
	xrb_seed_key (seed.bytes.data (), 0, prv.bytes.data ());
	ASSERT_FALSE (prv.is_zero ());
}

TEST (interface, xrb_key_account)
{
	nano::uint256_union prv (0);
	nano::uint256_union pub;
	xrb_key_account (prv.bytes.data (), pub.bytes.data ());
	ASSERT_FALSE (pub.is_zero ());
}

TEST (interface, sign_transaction)
{
	nano::raw_key key;
	xrb_generate_random (key.data.bytes.data ());
	nano::uint256_union pub;
	xrb_key_account (key.data.bytes.data (), pub.bytes.data ());
	nano::send_block send (0, 0, 0, key, pub, 0);
	ASSERT_FALSE (nano::validate_message (pub, send.hash (), send.signature));
	send.signature.bytes[0] ^= 1;
	ASSERT_TRUE (nano::validate_message (pub, send.hash (), send.signature));
	auto send_json (send.to_json ());
	auto transaction (xrb_sign_transaction (send_json.c_str (), key.data.bytes.data ()));
	boost::property_tree::ptree block_l;
	std::string transaction_l (transaction);
	std::stringstream block_stream (transaction_l);
	boost::property_tree::read_json (block_stream, block_l);
	auto block (nano::deserialize_block_json (block_l));
	ASSERT_NE (nullptr, block);
	auto send1 (dynamic_cast<nano::send_block *> (block.get ()));
	ASSERT_NE (nullptr, send1);
	ASSERT_FALSE (nano::validate_message (pub, send.hash (), send1->signature));
	// Signatures should be non-deterministic
	auto transaction2 (xrb_sign_transaction (send_json.c_str (), key.data.bytes.data ()));
	ASSERT_NE (0, strcmp (transaction, transaction2));
	free (transaction);
	free (transaction2);
}

TEST (interface, fail_sign_transaction)
{
	nano::uint256_union data (0);
	xrb_sign_transaction ("", data.bytes.data ());
}

TEST (interface, work_transaction)
{
	nano::raw_key key;
	xrb_generate_random (key.data.bytes.data ());
	nano::uint256_union pub;
	xrb_key_account (key.data.bytes.data (), pub.bytes.data ());
	nano::send_block send (1, 0, 0, key, pub, 0);
	auto transaction (xrb_work_transaction (send.to_json ().c_str ()));
	boost::property_tree::ptree block_l;
	std::string transaction_l (transaction);
	std::stringstream block_stream (transaction_l);
	boost::property_tree::read_json (block_stream, block_l);
	auto block (nano::deserialize_block_json (block_l));
	ASSERT_NE (nullptr, block);
	ASSERT_FALSE (nano::work_validate (*block));
	free (transaction);
}

TEST (interface, fail_work_transaction)
{
	nano::uint256_union data (0);
	xrb_work_transaction ("");
}
