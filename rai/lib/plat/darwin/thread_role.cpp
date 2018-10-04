#include <pthread.h>
#include <rai/lib/utility.hpp>

void rai::thread_role::set_name (std::string thread_name)
{
	pthread_setname_np (thread_name.c_str ());
	return;
}
