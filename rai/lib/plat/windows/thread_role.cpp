#include <windows.h>

#include <processthreadsapi.h>
#include <rai/lib/utility.hpp>

typedef HRESULT (*SetThreadDescription_t) (HANDLE, PCWSTR);

void rai::thread_role::set_name (std::string thread_name)
{
	SetThreadDescription_t SetThreadDescription_local;

	SetThreadDescription_local = (SetThreadDescription_t)GetProcAddress (GetModuleHandle (TEXT ("kernel32.dll")), "SetThreadDescription");
	if (SetThreadDescription_local)
	{
		std::wstring thread_name_wide (thread_name.begin (), thread_name.end ());
		SetThreadDescription_local (GetCurrentThread (), thread_name_wide.c_str ());
	}
}
