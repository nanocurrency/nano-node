#pragma once

#include <mutex>

namespace CryptoPP
{
class AutoSeededRandomPool;
}

namespace nano
{
/** While this uses CryptoPP do not call any of these functions from global scope, as they depend on global variables inside the CryptoPP library which may not have been initialized yet due to an undefined order for globals in different translation units. To make sure this is not an issue, there should be no ASAN warnings at startup on Mac/Clang in the CryptoPP files. */
class random_pool
{
public:
	static void generate_block (unsigned char * output, size_t size);
	static unsigned generate_word32 (unsigned min, unsigned max);
	/** Generates a random uint64_t in the range min to max. min and max are inclusive. */
	static uint64_t generate_word64 (uint64_t min, uint64_t max);
	static unsigned char generate_byte ();

	/** Fills variable with random data */
	template <class T>
	static void generate (T & out)
	{
		generate_block (reinterpret_cast<uint8_t *> (&out), sizeof (T));
	}
	/** Returns variable with random data */
	template <class T>
	static T generate ()
	{
		T t;
		generate (t);
		return t;
	}

public:
	random_pool () = delete;
	random_pool (random_pool const &) = delete;
	random_pool & operator= (random_pool const &) = delete;

private:
	static CryptoPP::AutoSeededRandomPool & get_pool ();

	template <class Iter>
	friend void random_pool_shuffle (Iter begin, Iter end);
};
}
