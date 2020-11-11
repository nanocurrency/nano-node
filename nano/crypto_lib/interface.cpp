#include <nano/crypto/blake2/blake2.h>
#include <nano/crypto_lib/random_pool.hpp>

extern "C" {
#include <crypto/ed25519-donna/ed25519-hash-custom.h>
void ed25519_randombytes_unsafe (void * out, size_t outlen)
{
	nano::random_pool::generate_block (reinterpret_cast<uint8_t *> (out), outlen);
}
void ed25519_hash_init (ed25519_hash_context * ctx)
{
	blake2b_init (ctx, 64);
}

void ed25519_hash_update (ed25519_hash_context * ctx, uint8_t const * in, size_t inlen)
{
	blake2b_update (ctx, in, inlen);
}

void ed25519_hash_final (ed25519_hash_context * ctx, uint8_t * out)
{
	blake2b_final (ctx, out, 64);
}

void ed25519_hash (uint8_t * out, uint8_t const * in, size_t inlen)
{
	ed25519_hash_context ctx;
	ed25519_hash_init (&ctx);
	ed25519_hash_update (&ctx, in, inlen);
	ed25519_hash_final (&ctx, out);
}
}
