#include <nano/lib/utility.hpp>

#include <boost/dll/runtime_symbol_info.hpp>

#include <iostream>

// Some builds (mac) fail due to "Boost.Stacktrace requires `_Unwind_Backtrace` function".
#ifndef _WIN32
#ifdef NANO_STACKTRACE_BACKTRACE
#define BOOST_STACKTRACE_USE_BACKTRACE
#endif
#ifndef _GNU_SOURCE
#define BEFORE_GNU_SOURCE 0
#define _GNU_SOURCE
#else
#define BEFORE_GNU_SOURCE 1
#endif
#endif
// On Windows this include defines min/max macros, so keep below other includes
// to reduce conflicts with other std functions
#include <boost/stacktrace.hpp>
#ifndef _WIN32
#if !BEFORE_GNU_SOURCE
#undef _GNU_SOURCE
#endif
#endif

namespace nano
{
seq_con_info_composite::seq_con_info_composite (const std::string & name) :
name (name)
{
}

bool seq_con_info_composite::is_composite () const
{
	return true;
}

void seq_con_info_composite::add_component (std::unique_ptr<seq_con_info_component> child)
{
	children.push_back (std::move (child));
}

const std::vector<std::unique_ptr<seq_con_info_component>> & seq_con_info_composite::get_children () const
{
	return children;
}

const std::string & seq_con_info_composite::get_name () const
{
	return name;
}

seq_con_info_leaf::seq_con_info_leaf (const seq_con_info & info) :
info (info)
{
}
bool seq_con_info_leaf::is_composite () const
{
	return false;
}
const seq_con_info & seq_con_info_leaf::get_info () const
{
	return info;
}

void dump_crash_stacktrace ()
{
	boost::stacktrace::safe_dump_to ("nano_node_backtrace.dump");
}

std::string generate_stacktrace ()
{
	auto stacktrace = boost::stacktrace::stacktrace ();
	std::stringstream ss;
	ss << stacktrace;
	return ss.str ();
}

namespace thread_role
{
	/*
	 * nano::thread_role namespace
	 *
	 * Manage thread role
	 */
	static thread_local nano::thread_role::name current_thread_role = nano::thread_role::name::unknown;
	nano::thread_role::name get ()
	{
		return current_thread_role;
	}

	std::string get_string (nano::thread_role::name role)
	{
		std::string thread_role_name_string;

		switch (role)
		{
			case nano::thread_role::name::unknown:
				thread_role_name_string = "<unknown>";
				break;
			case nano::thread_role::name::io:
				thread_role_name_string = "I/O";
				break;
			case nano::thread_role::name::work:
				thread_role_name_string = "Work pool";
				break;
			case nano::thread_role::name::packet_processing:
				thread_role_name_string = "Pkt processing";
				break;
			case nano::thread_role::name::alarm:
				thread_role_name_string = "Alarm";
				break;
			case nano::thread_role::name::vote_processing:
				thread_role_name_string = "Vote processing";
				break;
			case nano::thread_role::name::block_processing:
				thread_role_name_string = "Blck processing";
				break;
			case nano::thread_role::name::request_loop:
				thread_role_name_string = "Request loop";
				break;
			case nano::thread_role::name::wallet_actions:
				thread_role_name_string = "Wallet actions";
				break;
			case nano::thread_role::name::work_watcher:
				thread_role_name_string = "Work watcher";
				break;
			case nano::thread_role::name::bootstrap_initiator:
				thread_role_name_string = "Bootstrap init";
				break;
			case nano::thread_role::name::voting:
				thread_role_name_string = "Voting";
				break;
			case nano::thread_role::name::signature_checking:
				thread_role_name_string = "Signature check";
				break;
			case nano::thread_role::name::rpc_request_processor:
				thread_role_name_string = "RPC processor";
				break;
			case nano::thread_role::name::rpc_process_container:
				thread_role_name_string = "RPC process";
				break;
			case nano::thread_role::name::confirmation_height_processing:
				thread_role_name_string = "Conf height";
				break;
			case nano::thread_role::name::worker:
				thread_role_name_string = "Worker";
				break;
		}

		/*
		 * We want to constrain the thread names to 15
		 * characters, since this is the smallest maximum
		 * length supported by the platforms we support
		 * (specifically, Linux)
		 */
		assert (thread_role_name_string.size () < 16);
		return (thread_role_name_string);
	}

	std::string get_string ()
	{
		return get_string (current_thread_role);
	}

	void set (nano::thread_role::name role)
	{
		auto thread_role_name_string (get_string (role));

		nano::thread_role::set_os_name (thread_role_name_string);

		nano::thread_role::current_thread_role = role;
	}
}
}

void nano::thread_attributes::set (boost::thread::attributes & attrs)
{
	auto attrs_l (&attrs);
	attrs_l->set_stack_size (8000000); //8MB
}

nano::thread_runner::thread_runner (boost::asio::io_context & io_ctx_a, unsigned service_threads_a) :
io_guard (boost::asio::make_work_guard (io_ctx_a))
{
	boost::thread::attributes attrs;
	nano::thread_attributes::set (attrs);
	for (auto i (0u); i < service_threads_a; ++i)
	{
		threads.push_back (boost::thread (attrs, [&io_ctx_a]() {
			nano::thread_role::set (nano::thread_role::name::io);
			try
			{
				io_ctx_a.run ();
			}
			catch (std::exception const & ex)
			{
				std::cerr << ex.what () << std::endl;
#ifndef NDEBUG
				throw;
#endif
			}
			catch (...)
			{
#ifndef NDEBUG
				/*
				 * In a release build, catch and swallow the
				 * io_context exception, in debug mode pass it
				 * on
				 */
				throw;
#endif
			}
		}));
	}
}

nano::thread_runner::~thread_runner ()
{
	join ();
}

void nano::thread_runner::join ()
{
	io_guard.reset ();
	for (auto & i : threads)
	{
		if (i.joinable ())
		{
			i.join ();
		}
	}
}

void nano::thread_runner::stop_event_processing ()
{
	io_guard.get_executor ().context ().stop ();
}

nano::worker::worker () :
thread ([this]() {
	nano::thread_role::set (nano::thread_role::name::worker);
	this->run ();
})
{
}

void nano::worker::run ()
{
	while (!stopped)
	{
		nano::unique_lock<std::mutex> lk (mutex);
		if (!queue.empty ())
		{
			auto func = queue.front ();
			queue.pop_front ();
			lk.unlock ();
			func ();
			// So that we reduce locking for anything being pushed as that will
			// most likely be on an io-thread
			std::this_thread::yield ();
		}
		else
		{
			cv.wait (lk);
		}
	}
}

nano::worker::~worker ()
{
	stop ();
}

void nano::worker::push_task (std::function<void()> func_a)
{
	{
		nano::lock_guard<std::mutex> guard (mutex);
		queue.emplace_back (func_a);
	}

	cv.notify_one ();
}

void nano::worker::stop ()
{
	stopped = true;
	cv.notify_one ();
	if (thread.joinable ())
	{
		thread.join ();
	}
}

std::unique_ptr<nano::seq_con_info_component> nano::collect_seq_con_info (nano::worker & worker, const std::string & name)
{
	auto composite = std::make_unique<seq_con_info_composite> (name);

	size_t count = 0;
	{
		nano::lock_guard<std::mutex> guard (worker.mutex);
		count = worker.queue.size ();
	}
	auto sizeof_element = sizeof (decltype (worker.queue)::value_type);
	composite->add_component (std::make_unique<nano::seq_con_info_leaf> (nano::seq_con_info{ "queue", count, sizeof_element }));
	return composite;
}

void nano::remove_all_files_in_dir (boost::filesystem::path const & dir)
{
	for (auto & p : boost::filesystem::directory_iterator (dir))
	{
		auto path = p.path ();
		if (boost::filesystem::is_regular_file (path))
		{
			boost::filesystem::remove (path);
		}
	}
}

void nano::move_all_files_to_dir (boost::filesystem::path const & from, boost::filesystem::path const & to)
{
	for (auto & p : boost::filesystem::directory_iterator (from))
	{
		auto path = p.path ();
		if (boost::filesystem::is_regular_file (path))
		{
			boost::filesystem::rename (path, to / path.filename ());
		}
	}
}

/*
 * Backing code for "release_assert", which is itself a macro
 */
void release_assert_internal (bool check, const char * check_expr, const char * file, unsigned int line)
{
	if (check)
	{
		return;
	}

	std::cerr << "Assertion (" << check_expr << ") failed " << file << ":" << line << "\n\n";

	// Output stack trace to cerr
	auto backtrace_str = nano::generate_stacktrace ();
	std::cerr << backtrace_str << std::endl;

	// "abort" at the end of this function will go into any signal handlers (the daemon ones will generate a stack trace and load memory address files on non-Windows systems).
	// As there is no async-signal-safe way to generate stacktraces on Windows so must be done before aborting
#ifdef _WIN32
	{
		// Try construct the stacktrace dump in the same folder as the the running executable, otherwise use the current directory.
		boost::system::error_code err;
		auto running_executable_filepath = boost::dll::program_location (err);
		std::string filename = "nano_node_backtrace_release_assert.txt";
		std::string filepath = filename;
		if (!err)
		{
			filepath = (running_executable_filepath.parent_path () / filename).string ();
		}

		std::ofstream file (filepath);
		nano::set_secure_perm_file (filepath);
		file << backtrace_str;
	}
#endif
	abort ();
}
