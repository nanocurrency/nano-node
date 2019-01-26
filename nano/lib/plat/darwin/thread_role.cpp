#include <nano/lib/utility.hpp>
#include <pthread.h>

void nano::thread_role::set_os_name (std::string thread_name)
{
	pthread_setname_np (thread_name.c_str ());
}
