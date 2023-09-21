#include <nano/lib/thread_roles.hpp>

#include <windows.h>

void nano::thread_role::set_os_name (std::string const & thread_name)
{
	using SetThreadDescription_t = HRESULT (*) (HANDLE, PCWSTR);
	SetThreadDescription_t SetThreadDescription_local = (SetThreadDescription_t)GetProcAddress (GetModuleHandle (TEXT ("kernel32.dll")), "SetThreadDescription");
	if (SetThreadDescription_local)
	{
		std::wstring thread_name_wide (thread_name.begin (), thread_name.end ());
		SetThreadDescription_local (GetCurrentThread (), thread_name_wide.c_str ());
	}
}
