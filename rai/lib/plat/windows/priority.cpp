#include <rai/lib/utility.hpp>

#include <windows.h>

void rai::work_thread_reprioritize ()
{
	auto SUCCESS (SetThreadPriority (GetCurrentThread (), THREAD_MODE_BACKGROUND_BEGIN));
}
