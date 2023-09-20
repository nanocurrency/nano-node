#include <nano/lib/thread_roles.hpp>

#include <pthread.h>

void nano::thread_role::set_os_name (std::string const & thread_name)
{
	pthread_setname_np (pthread_self (), thread_name.c_str ());
}
