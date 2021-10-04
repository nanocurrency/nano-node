#pragma once

#include <nano/crypto_lib/random_pool.hpp>

#include <crypto/cryptopp/osrng.h>

namespace nano
{
template <class Iter>
void random_pool_shuffle (Iter begin, Iter end)
{
	std::lock_guard guard (random_pool::mutex);
	random_pool::get_pool ().Shuffle (begin, end);
}
}
