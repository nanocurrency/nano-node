#include <iostream>
#include <rai/lib/utility.hpp>

namespace rai
{
namespace thread_role
{

	/*
	 * rai::thread_role namespace
	 *
	 * Manage thread role
	 */
	static thread_local rai::thread_role::name current_thread_role = rai::thread_role::name::unknown;
	rai::thread_role::name get (void)
	{
		return current_thread_role;
	}

	void set (rai::thread_role::name role)
	{
		rai::thread_role::current_thread_role = role;
	}
}
}

/*
 * Backing code for "release_assert", which is itself a macro
 */
void release_assert_internal (bool check, const char * check_expr, const char * file, unsigned int line)
{
	if (check)
	{
		return;
	}

	std::cerr << "Assertion (" << check_expr << ") failed " << file << ":" << line << std::endl;
	abort ();
}
