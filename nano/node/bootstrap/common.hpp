#pragma once

#include <nano/crypto_lib/random_pool.hpp>

namespace nano::bootstrap
{
using id_t = uint64_t;
static nano::bootstrap::id_t generate_id ()
{
	nano::bootstrap::id_t id;
	nano::random_pool::generate_block (reinterpret_cast<uint8_t *> (&id), sizeof (id));
	return id;
}
}
