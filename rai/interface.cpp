#include <rai/interface.h>

#include <xxhash/xxhash.h>

#include <ed25519-donna/ed25519.h>

#include <blake2/blake2.h>

#include <rai/numbers.hpp>

extern "C" {
void xrb_uint256_to_string (xrb_uint256 source, char * destination)
{

}

void xrb_uint512_to_string (xrb_uint512 source, char * destination)
{

}

int xrb_uint256_from_string (char * source, xrb_uint256 destination)
{
	return 1;
}

int xrb_uint512_from_string (char * source, xrb_uint512 destination)
{
	return 1;
}

int xrb_valid_address (char * account)
{
	return 1;
}

void sign_transaction (char * transaction, xrb_uint256 private_key, xrb_uint512 signature)
{

}

#include <ed25519-donna/ed25519-hash-custom.h>
void ed25519_randombytes_unsafe (void * out, size_t outlen)
{
	rai::random_pool.GenerateBlock (reinterpret_cast <uint8_t *> (out), outlen);
}
void ed25519_hash_init (ed25519_hash_context * ctx)
{
	ctx->blake2 = new blake2b_state;
	blake2b_init (reinterpret_cast <blake2b_state *> (ctx->blake2), 64);
}

void ed25519_hash_update (ed25519_hash_context * ctx, uint8_t const * in, size_t inlen)
{
	blake2b_update (reinterpret_cast <blake2b_state *> (ctx->blake2), in, inlen);
}

void ed25519_hash_final (ed25519_hash_context * ctx, uint8_t * out)
{
	blake2b_final (reinterpret_cast <blake2b_state *> (ctx->blake2), out, 64);
	delete reinterpret_cast <blake2b_state *> (ctx->blake2);
}

void ed25519_hash (uint8_t * out, uint8_t const * in, size_t inlen)
{
	ed25519_hash_context ctx;
	ed25519_hash_init (&ctx);
	ed25519_hash_update (&ctx, in, inlen);
	ed25519_hash_final (&ctx, out);
}
}
