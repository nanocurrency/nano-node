#include <galileo/lib/utility.hpp>

#include <windows.h>

void galileo::work_thread_reprioritize ()
{
	auto SUCCESS (SetThreadPriority (GetCurrentThread (), THREAD_MODE_BACKGROUND_BEGIN));
}
