#include <rai/lib/interface.h>

#include <xxhash/xxhash.h>

#include <ed25519-donna/ed25519.h>

#include <blake2/blake2.h>

#include <boost/property_tree/json_parser.hpp>

#include <rai/lib/blocks.hpp>
#include <rai/lib/numbers.hpp>
#include <rai/lib/work.hpp>

#include <cstring>

extern "C" {
void xrb_uint128_to_dec (xrb_uint128 source, char * destination)
{
	auto const & number (*reinterpret_cast<rai::uint128_union *> (source));
	strncpy (destination, number.to_string_dec ().c_str (), 32);
}

void xrb_uint256_to_string (xrb_uint256 source, char * destination)
{
	auto const & number (*reinterpret_cast<rai::uint256_union *> (source));
	strncpy (destination, number.to_string ().c_str (), 64);
}

void xrb_uint256_to_address (xrb_uint256 source, char * destination)
{
	auto const & number (*reinterpret_cast<rai::uint256_union *> (source));
	strncpy (destination, number.to_account ().c_str (), 65);
}

void xrb_uint512_to_string (xrb_uint512 source, char * destination)
{
	auto const & number (*reinterpret_cast<rai::uint512_union *> (source));
	strncpy (destination, number.to_string ().c_str (), 128);
}

int xrb_uint128_from_dec (const char * source, xrb_uint128 destination)
{
	auto & number (*reinterpret_cast<rai::uint128_union *> (destination));
	auto error (number.decode_dec (source));
	return error ? 1 : 0;
}

int xrb_uint256_from_string (const char * source, xrb_uint256 destination)
{
	auto & number (*reinterpret_cast<rai::uint256_union *> (destination));
	auto error (number.decode_hex (source));
	return error ? 1 : 0;
}

int xrb_uint512_from_string (const char * source, xrb_uint512 destination)
{
	auto & number (*reinterpret_cast<rai::uint512_union *> (destination));
	auto error (number.decode_hex (source));
	return error ? 1 : 0;
}

int xrb_valid_address (const char * account_a)
{
	rai::uint256_union account;
	auto error (account.decode_account (account_a));
	return error ? 1 : 0;
}

void xrb_generate_random (xrb_uint256 seed)
{
	auto & number (*reinterpret_cast<rai::uint256_union *> (seed));
	rai::random_pool.GenerateBlock (number.bytes.data (), number.bytes.size ());
}

void xrb_seed_key (xrb_uint256 seed, int index, xrb_uint256 destination)
{
	auto & seed_l (*reinterpret_cast<rai::uint256_union *> (seed));
	auto & destination_l (*reinterpret_cast<rai::uint256_union *> (destination));
	rai::deterministic_key (seed_l, index, destination_l);
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
		auto block (rai::deserialize_block_json (block_l));
		if (block != nullptr)
		{
			rai::uint256_union pub;
			ed25519_publickey (private_key, pub.bytes.data ());
			rai::raw_key prv;
			prv.data = *reinterpret_cast<rai::uint256_union *> (private_key);
			block->signature_set (rai::sign_message (prv, pub, block->hash ()));
			auto json (block->to_json ());
			result = reinterpret_cast<char *> (malloc (json.size () + 1));
			strncpy (result, json.c_str (), json.size () + 1);
		}
	}
	catch (std::runtime_error const & err)
	{
	}
	return result;
}

char * xrb_work_transaction (const char * transaction)
{
	char * result (nullptr);
	try
	{
		boost::property_tree::ptree block_l;
		std::string transaction_l (transaction);
		std::stringstream block_stream (transaction_l);
		boost::property_tree::read_json (block_stream, block_l);
		auto block (rai::deserialize_block_json (block_l));
		if (block != nullptr)
		{
			rai::work_pool pool (std::thread::hardware_concurrency ());
			auto work (pool.generate (block->root ()));
			block->block_work_set (work);
			auto json (block->to_json ());
			result = reinterpret_cast<char *> (malloc (json.size () + 1));
			strncpy (result, json.c_str (), json.size () + 1);
		}
	}
	catch (std::runtime_error const & err)
	{
	}
	return result;
}

#include <ed25519-donna/ed25519-hash-custom.h>
void ed25519_randombytes_unsafe (void * out, size_t outlen)
{
	rai::random_pool.GenerateBlock (reinterpret_cast<uint8_t *> (out), outlen);
}
void ed25519_hash_init (ed25519_hash_context * ctx)
{
	ctx->blake2 = new blake2b_state;
	blake2b_init (reinterpret_cast<blake2b_state *> (ctx->blake2), 64);
}

void ed25519_hash_update (ed25519_hash_context * ctx, uint8_t const * in, size_t inlen)
{
	blake2b_update (reinterpret_cast<blake2b_state *> (ctx->blake2), in, inlen);
}

void ed25519_hash_final (ed25519_hash_context * ctx, uint8_t * out)
{
	blake2b_final (reinterpret_cast<blake2b_state *> (ctx->blake2), out, 64);
	delete reinterpret_cast<blake2b_state *> (ctx->blake2);
}

void ed25519_hash (uint8_t * out, uint8_t const * in, size_t inlen)
{
	ed25519_hash_context ctx;
	ed25519_hash_init (&ctx);
	ed25519_hash_update (&ctx, in, inlen);
	ed25519_hash_final (&ctx, out);
}
}
