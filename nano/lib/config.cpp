#include <nano/lib/config.hpp>

#include <valgrind/valgrind.h>

namespace nano
{
void force_nano_test_network ()
{
	nano::network_constants::set_active_network (nano::nano_networks::nano_test_network);
}

bool running_within_valgrind ()
{
	return (RUNNING_ON_VALGRIND > 0);
}
}
