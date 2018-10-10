#include <iostream>
#include <rai/lib/utility.hpp>

void rai::thread_attributes::set (boost::thread::attributes & attrs)
{
	auto attrs_l (&attrs);
	attrs_l->set_stack_size (8000000); //8MB
}

void release_assert_internal (bool check, const char * check_expr, const char * file, unsigned int line)
{
	if (check)
	{
		return;
	}

	std::cerr << "Assertion (" << check_expr << ") failed " << file << ":" << line << std::endl;
	abort ();
}
