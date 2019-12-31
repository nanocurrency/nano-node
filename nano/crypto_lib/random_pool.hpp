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
	static unsigned char generate_byte ();

	random_pool () = delete;
	random_pool (random_pool const &) = delete;
	random_pool & operator= (random_pool const &) = delete;

private:
	static std::mutex mutex;
	static CryptoPP::AutoSeededRandomPool & get_pool ();

	template <class Iter>
	friend void random_pool_shuffle (Iter begin, Iter end);
};
}