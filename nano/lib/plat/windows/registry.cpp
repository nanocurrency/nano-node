#include <windows.h>

namespace nano
{
bool event_log_reg_entry_exists ()
{
	HKEY hKey;
	auto res = RegOpenKeyExW (HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\EventLog\\Nano\\Nano", 0, KEY_READ, &hKey);
	auto found_key = (res == ERROR_SUCCESS);
	if (found_key)
	{
		RegCloseKey (hKey);
	}
	return found_key;
}
}
