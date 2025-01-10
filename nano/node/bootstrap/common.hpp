#pragma once

#include <celerix/crypto_lib/random_pool.hpp>

namespace celerix::bootstrap
{
using id_t = uint64_t;
static celerix::bootstrap::id_t generate_id ()
{
	celerix::bootstrap::id_t id;
	celerix::random_pool::generate_block (reinterpret_cast<uint8_t *> (&id), sizeof (id));
	return id;
}
}
