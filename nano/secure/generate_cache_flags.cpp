#include <nano/secure/generate_cache_flags.hpp>

void nano::generate_cache_flags::enable_all ()
{
	reps = true;
	cemented_count = true;
	unchecked_count = true;
	account_count = true;
}
