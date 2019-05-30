#include <nano/crypto_lib/random_pool.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/config.hpp>
#include <nano/lib/interface.h>
#include <nano/lib/numbers.hpp>
#include <nano/lib/work.hpp>

#include <boost/property_tree/json_parser.hpp>

#include <cstring>

#include <crypto/ed25519-donna/ed25519.h>

extern "C" {
void xrb_uint128_to_dec (xrb_uint128 source, char * destination)
{
	auto const & number (*reinterpret_cast<nano::uint128_union *> (source));
	strncpy (destination, number.to_string_dec ().c_str (), 40);
}

void xrb_uint256_to_string (xrb_uint256 source, char * destination)
{
	auto const & number (*reinterpret_cast<nano::uint256_union *> (source));
	strncpy (destination, number.to_string ().c_str (), 65);
}

void xrb_uint256_to_address (xrb_uint256 source, char * destination)
{
	auto const & number (*reinterpret_cast<nano::uint256_union *> (source));
	strncpy (destination, number.to_account ().c_str (), 65);
}

void xrb_uint512_to_string (xrb_uint512 source, char * destination)
{
	auto const & number (*reinterpret_cast<nano::uint512_union *> (source));
	strncpy (destination, number.to_string ().c_str (), 129);
}

int xrb_uint128_from_dec (const char * source, xrb_uint128 destination)
{
	auto & number (*reinterpret_cast<nano::uint128_union *> (destination));
	auto error (number.decode_dec (source));
	return error ? 1 : 0;
}

int xrb_uint256_from_string (const char * source, xrb_uint256 destination)
{
	auto & number (*reinterpret_cast<nano::uint256_union *> (destination));
	auto error (number.decode_hex (source));
	return error ? 1 : 0;
}

int xrb_uint512_from_string (const char * source, xrb_uint512 destination)
{
	auto & number (*reinterpret_cast<nano::uint512_union *> (destination));
	auto error (number.decode_hex (source));
	return error ? 1 : 0;
}

int xrb_valid_address (const char * account_a)
{
	nano::uint256_union account;
	auto error (account.decode_account (account_a));
	return error ? 1 : 0;
}

void xrb_generate_random (xrb_uint256 seed)
{
	auto & number (*reinterpret_cast<nano::uint256_union *> (seed));
	nano::random_pool::generate_block (number.bytes.data (), number.bytes.size ());
}

void xrb_seed_key (xrb_uint256 seed, int index, xrb_uint256 destination)
{
	auto & seed_l (*reinterpret_cast<nano::uint256_union *> (seed));
	auto & destination_l (*reinterpret_cast<nano::uint256_union *> (destination));
	nano::deterministic_key (seed_l, index, destination_l);
}

void xrb_key_account (const xrb_uint256 key, xrb_uint256 pub)
{
	ed25519_publickey (key, pub);
}

char * xrb_sign_transaction (const char * transaction, const xrb_uint256 private_key)
{
	char * result (nullptr);
	try
	{
		boost::property_tree::ptree block_l;
		std::string transaction_l (transaction);
		std::stringstream block_stream (transaction_l);
		boost::property_tree::read_json (block_stream, block_l);
		auto block (nano::deserialize_block_json (block_l));
		if (block != nullptr)
		{
			nano::uint256_union pub;
			ed25519_publickey (private_key, pub.bytes.data ());
			nano::raw_key prv;
			prv.data = *reinterpret_cast<nano::uint256_union *> (private_key);
			block->signature_set (nano::sign_message (prv, pub, block->hash ()));
			auto json (block->to_json ());
			result = reinterpret_cast<char *> (malloc (json.size () + 1));
			strncpy (result, json.c_str (), json.size () + 1);
		}
	}
	catch (std::runtime_error const &)
	{
	}
	return result;
}

char * xrb_work_transaction (const char * transaction)
{
	static nano::network_constants network_constants;
	char * result (nullptr);
	try
	{
		boost::property_tree::ptree block_l;
		std::string transaction_l (transaction);
		std::stringstream block_stream (transaction_l);
		boost::property_tree::read_json (block_stream, block_l);
		auto block (nano::deserialize_block_json (block_l));
		if (block != nullptr)
		{
			nano::work_pool pool (boost::thread::hardware_concurrency ());
			auto work (pool.generate (block->root (), network_constants.publish_threshold));
			block->block_work_set (work);
			auto json (block->to_json ());
			result = reinterpret_cast<char *> (malloc (json.size () + 1));
			strncpy (result, json.c_str (), json.size () + 1);
		}
	}
	catch (std::runtime_error const &)
	{
	}
	return result;
}
}
