#pragma once

#include <boost/filesystem.hpp>
#include <boost/system/error_code.hpp>

#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace rai
{
// Lower priority of calling work generating thread
void work_thread_reprioritize ();

/*
 * Functions for managing filesystem permissions, platform specific
 */
void set_umask ();
void set_secure_perm_directory (boost::filesystem::path const & path);
void set_secure_perm_directory (boost::filesystem::path const & path, boost::system::error_code & ec);
void set_secure_perm_file (boost::filesystem::path const & path);
void set_secure_perm_file (boost::filesystem::path const & path, boost::system::error_code & ec);

/*
 * Functions for understanding the role of the current thread
 */
namespace thread_role
{
	enum class name
	{
		unknown,
		io,
		work,
		packet_processing,
		alarm,
		vote_processing,
		block_processing,
		announce_loop,
	};
	rai::thread_role::name get (void);
	void set (rai::thread_role::name);
	void set_name (std::string);
}

template <typename... T>
class observer_set
{
public:
	void add (std::function<void(T...)> const & observer_a)
	{
		std::lock_guard<std::mutex> lock (mutex);
		observers.push_back (observer_a);
	}
	void notify (T... args)
	{
		std::lock_guard<std::mutex> lock (mutex);
		for (auto & i : observers)
		{
			i (args...);
		}
	}
	std::mutex mutex;
	std::vector<std::function<void(T...)>> observers;
};
}

void release_assert_internal (bool check, const char * check_expr, const char * file, unsigned int line);
#define release_assert(check) release_assert_internal (check, #check, __FILE__, __LINE__)
