#pragma once

#include <nano/crypto_lib/random_pool.hpp>

#include <cstdlib>

namespace nano::bootstrap_ascending
{
using id_t = uint64_t;
static nano::bootstrap_ascending::id_t generate_id ()
{
	nano::bootstrap_ascending::id_t id;
	nano::random_pool::generate_block (reinterpret_cast<uint8_t *> (&id), sizeof (id));
	return id;
}
} // nano::bootstrap_ascending
