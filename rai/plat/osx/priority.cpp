#include <rai/utility.hpp>

#include <pthread.h>

void rai::lower_priority ()
{
	auto handle (pthread_self ());
	int policy;
	struct sched_param sched;
	if (pthread_getschedparam (handle, &policy, &sched) == 0)
	{
		policy = SCHED_BATCH;
		auto result (pthread_setschedparam (handle, policy, &sched));
		(void) result;
	}
}