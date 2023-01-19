#include <nano/crypto_lib/random_pool.hpp>

#include <crypto/cryptopp/misc.h>
#include <crypto/cryptopp/osrng.h>

void nano::random_pool::generate_block (unsigned char * output, size_t size)
{
	auto & pool = get_pool ();
	pool.GenerateBlock (output, size);
}

unsigned nano::random_pool::generate_word32 (unsigned min, unsigned max)
{
	auto & pool = get_pool ();
	return pool.GenerateWord32 (min, max);
}

uint64_t nano::random_pool::generate_word64 (uint64_t min, uint64_t max)
{
	auto & pool = get_pool ();

	const auto range = max - min;
	const auto max_bits = CryptoPP::BitPrecision (range);

	uint64_t value;

	do
	{
		pool.GenerateBlock ((unsigned char *)&value, sizeof (value));
		value = CryptoPP::Crop (value, max_bits);
	} while (value > range);

	return value + min;
}

unsigned char nano::random_pool::generate_byte ()
{
	auto & pool = get_pool ();
	return pool.GenerateByte ();
}

CryptoPP::AutoSeededRandomPool & nano::random_pool::get_pool ()
{
	static thread_local CryptoPP::AutoSeededRandomPool pool;
	return pool;
}
