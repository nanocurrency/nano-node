#include <pthread.h>
#include <pthread_np.h>
#include <rai/lib/utility.hpp>

void rai::thread_role::set_name (std::string thread_name)
{
	pthread_set_name_np (pthread_self (), thread_name.c_str ());
}
