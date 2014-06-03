/*
	a custom hash must have a 512bit digest and implement:

	struct ed25519_hash_context;

	void ed25519_hash_init(ed25519_hash_context *ctx);
	void ed25519_hash_update(ed25519_hash_context *ctx, const uint8_t *in, size_t inlen);
	void ed25519_hash_final(ed25519_hash_context *ctx, uint8_t *hash);
	void ed25519_hash(uint8_t *hash, const uint8_t *in, size_t inlen);
*/

#include <cryptopp/sha3.h>

struct ed25519_hash_context
{
    ed25519_hash_context () :
    sha (64)
    {
    }
    CryptoPP::SHA3 sha;
};

void ed25519_hash_init (ed25519_hash_context * ctx)
{
}

void ed25519_hash_update (ed25519_hash_context * ctx, uint8_t const * in, size_t inlen)
{
    ctx->sha.Update (in, inlen);
}

void ed25519_hash_final (ed25519_hash_context * ctx, uint8_t * out)
{
    ctx->sha.Final (out);
}

void ed25519_hash (uint8_t * out, uint8_t const * in, size_t inlen)
{
    ed25519_hash_context ctx;
    ctx.sha.Update (in, inlen);
    ctx.sha.Final (out);
}