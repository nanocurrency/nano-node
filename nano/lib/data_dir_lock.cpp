#include <nano/lib/data_dir_lock.hpp>

#include <stdexcept>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

nano::data_dir_lock::data_dir_lock (std::filesystem::path const & data_path) :
	path{ lock_file (data_path) }
{
#ifdef _WIN32
	// The handle is not inheritable, so child processes such as the RPC server never hold the lock
	HANDLE file = ::CreateFileW (path.c_str (), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
	{
		throw std::system_error (::GetLastError (), std::system_category (), "Unable to open data directory lock file: " + path.string ());
	}
	OVERLAPPED overlapped{};
	if (!::LockFileEx (file, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, MAXDWORD, MAXDWORD, &overlapped))
	{
		auto const error = ::GetLastError ();
		::CloseHandle (file);
		if (error == ERROR_LOCK_VIOLATION)
		{
			throw std::runtime_error ("Another process holds the data directory lock: " + path.string ());
		}
		throw std::system_error (error, std::system_category (), "Unable to lock data directory lock file: " + path.string ());
	}
	handle = reinterpret_cast<std::intptr_t> (file);
#else
	// Closed on exec, so child processes such as the RPC server never hold the lock
	int descriptor = ::open (path.c_str (), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
	if (descriptor == -1)
	{
		throw std::system_error (errno, std::generic_category (), "Unable to open data directory lock file: " + path.string ());
	}
	if (::flock (descriptor, LOCK_EX | LOCK_NB) == -1)
	{
		auto const error = errno;
		::close (descriptor);
		if (error == EWOULDBLOCK)
		{
			throw std::runtime_error ("Another process holds the data directory lock: " + path.string ());
		}
		throw std::system_error (error, std::generic_category (), "Unable to lock data directory lock file: " + path.string ());
	}
	handle = descriptor;
#endif
}

nano::data_dir_lock::~data_dir_lock ()
{
#ifdef _WIN32
	auto file = reinterpret_cast<HANDLE> (handle);
	OVERLAPPED overlapped{};
	::UnlockFileEx (file, 0, MAXDWORD, MAXDWORD, &overlapped);
	::CloseHandle (file);
#else
	// Closing the descriptor releases the lock as well
	::close (static_cast<int> (handle));
#endif
}

std::filesystem::path nano::data_dir_lock::lock_file (std::filesystem::path const & data_path)
{
	return data_path / filename;
}
