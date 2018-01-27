#include <iostream>
#include <rai/lib/numbers.hpp>

static CryptoPP::AutoSeededRandomPool random_pool;

/**
 * This program redirects an infinite stream of bytes from the random pool
 * to standard out.
 *
 * The result can be fed into various tools for testing random generators
 * and entropy pools.
 *
 * Example, running the entire dieharder test suite:
 *
 *   ./random_test | dieharder -a -g 200
 */
int main (int argc, char * const * argv)
{
	rai::raw_key seed;
	for (;;)
	{
		random_pool.GenerateBlock (seed.data.bytes.data (), seed.data.bytes.size ());
		std::cout.write ((const char *)seed.data.bytes.data (), seed.data.bytes.size ());
	}
	return 0;
}
