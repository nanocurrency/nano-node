#include <nano/crypto_lib/secure_memory.hpp>
#include <nano/lib/assert.hpp>
#include <nano/lib/kdf.hpp>

#include <argon2.h>

namespace nano
{
void kdf::phs (nano::raw_key & result_a, std::string const & password_a, nano::uint256_union const & salt_a)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	auto success (argon2_hash (1, kdf_work, 1, password_a.data (), password_a.size (), salt_a.bytes.data (), salt_a.bytes.size (), result_a.bytes.data (), result_a.bytes.size (), NULL, 0, Argon2_d, 0x10));
	// argon2 leaves blocks derived from the password in its stack frames
	nano::secure_wipe_stack ();
	release_assert (success == 0);
	(void)success;
}
}
