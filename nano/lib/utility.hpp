#pragma once

#include <nano/lib/locks.hpp>

#include <functional>
#include <mutex>
#include <vector>

namespace boost
{
namespace filesystem
{
	class path;
}

namespace system
{
	class error_code;
}
}

namespace nano
{
/* These containers are used to collect information about sequence containers.
 * It makes use of the composite design pattern to collect information
 * from sequence containers and sequence containers inside member variables.
 */
struct container_info
{
	std::string name;
	size_t count;
	size_t sizeof_element;
};

class container_info_component
{
public:
	virtual ~container_info_component () = default;
	virtual bool is_composite () const = 0;
};

class container_info_composite : public container_info_component
{
public:
	container_info_composite (const std::string & name);
	bool is_composite () const override;
	void add_component (std::unique_ptr<container_info_component> child);
	const std::vector<std::unique_ptr<container_info_component>> & get_children () const;
	const std::string & get_name () const;

private:
	std::string name;
	std::vector<std::unique_ptr<container_info_component>> children;
};

class container_info_leaf : public container_info_component
{
public:
	container_info_leaf (container_info const & info);
	bool is_composite () const override;
	const container_info & get_info () const;

private:
	container_info info;
};

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
 * Function to check if running Windows as an administrator
 */
bool is_windows_elevated ();

/*
 * Function to check if the Windows Event log registry key exists
 */
bool event_log_reg_entry_exists ();

/*
 * Create the load memory addresses for the executable and shared libraries.
 */
void create_load_memory_address_files ();

/*
 * Dumps a stacktrace file which can be read using the --debug_output_last_backtrace_dump CLI command
 */
void dump_crash_stacktrace ();

/*
 * Generates the current stacktrace
 */
std::string generate_stacktrace ();

/**
 * Returns seconds passed since unix epoch (posix time)
 */
inline uint64_t seconds_since_epoch ()
{
	return std::chrono::duration_cast<std::chrono::seconds> (std::chrono::system_clock::now ().time_since_epoch ()).count ();
}

template <typename... T>
class observer_set final
{
public:
	void add (std::function<void(T...)> const & observer_a)
	{
		nano::lock_guard<std::mutex> lock (mutex);
		observers.push_back (observer_a);
	}
	void notify (T... args)
	{
		nano::lock_guard<std::mutex> lock (mutex);
		for (auto & i : observers)
		{
			i (args...);
		}
	}
	std::mutex mutex;
	std::vector<std::function<void(T...)>> observers;
};

template <typename... T>
std::unique_ptr<container_info_component> collect_container_info (observer_set<T...> & observer_set, const std::string & name)
{
	size_t count = 0;
	{
		nano::lock_guard<std::mutex> lock (observer_set.mutex);
		count = observer_set.observers.size ();
	}

	auto sizeof_element = sizeof (typename decltype (observer_set.observers)::value_type);
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "observers", count, sizeof_element }));
	return composite;
}

void remove_all_files_in_dir (boost::filesystem::path const & dir);
void move_all_files_to_dir (boost::filesystem::path const & from, boost::filesystem::path const & to);
}
// Have our own async_write which we must use?

void release_assert_internal (bool check, const char * check_expr, const char * file, unsigned int line);
#define release_assert(check) release_assert_internal (check, #check, __FILE__, __LINE__)
