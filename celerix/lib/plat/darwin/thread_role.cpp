#include <celerix/lib/thread_roles.hpp>

#include <pthread.h>

void celerix::thread_role::set_os_name (std::string const & thread_name)
{
	pthread_setname_np (thread_name.c_str ());
}
