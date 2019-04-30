#include <nano/node/lmdb_txn_tracker.hpp>

#include <nano/lib/jsonconfig.hpp>
#include <nano/lib/utility.hpp>
#include <nano/lib/logger_mt.hpp>

// Some builds (mac) fail due to "Boost.Stacktrace requires `_Unwind_Backtrace` function".
#ifndef _WIN32
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

nano::mdb_txn_tracker::mdb_txn_tracker (nano::logger_mt & logger_a) :
logger (logger_a)
{
}

void nano::mdb_txn_tracker::serialize_json (boost::property_tree::ptree & json, std::chrono::seconds min_time)
{
	// Copying is cheap compared to generating the stack trace strings, so reduce time holding the mutex
	std::vector<mdb_txn_stats> copy_stats;
	{
		std::lock_guard<std::mutex> guard (mutex);
		copy_stats = stats;
	}

	// Get the time difference now as creating stacktraces can take a while so results won't be as accurate
	std::vector<std::chrono::seconds> time_since_starts;
	time_since_starts.reserve (copy_stats.size ());
	// clang-format off
	std::transform (copy_stats.cbegin (), copy_stats.cend (), std::back_inserter (time_since_starts), [] (const auto & stat) {
		return stat.timer.since_start ();
	});
	// clang-format on
	assert (time_since_starts.size () == copy_stats.size ());

	for (size_t i = 0; i < time_since_starts.size (); ++i)
	{
		auto const & stat = copy_stats[i];
		auto time_locked = time_since_starts[i];

		if (time_locked >= min_time)
		{
			nano::jsonconfig mdb_lock_config;

			mdb_lock_config.put ("thread", stat.thread_name);
			mdb_lock_config.put ("time_locked", time_locked.count ());
			mdb_lock_config.put ("write", stat.is_write);

			boost::property_tree::ptree stacktrace_config;
			for (auto frame : *stat.stacktrace)
			{
				nano::jsonconfig frame_json;
				frame_json.put ("name", frame.name ());
				frame_json.put ("address", frame.address ());
				frame_json.put ("source_file", frame.source_file ());
				frame_json.put ("source_line", frame.source_line ());
				stacktrace_config.push_back (std::make_pair ("", frame_json.get_tree ()));
			}

			nano::jsonconfig stack (stacktrace_config);
			mdb_lock_config.put_child ("stacktrace", stack);
			json.push_back (std::make_pair ("", mdb_lock_config.get_tree ()));
		}
	}
}

void nano::mdb_txn_tracker::output_finished (nano::mdb_txn_stats & mdb_txn_stats)
{
	// Only output them if locks were held for longer than a certain period of time
	if (mdb_txn_stats.timer.since_start () >= min_time_locked_ouput)
	{
		logger.always_log (boost::str (boost::format ("%1%s (%2%) lock held for on thread: %3%\n%4%") % mdb_txn_stats.timer.since_start ().count () % (mdb_txn_stats.is_write ? "write" : "read") % mdb_txn_stats.thread_name % mdb_txn_stats.stacktrace));
	}
}

void nano::mdb_txn_tracker::add (nano::transaction_impl * transaction_impl, bool is_write_a)
{
	std::lock_guard<std::mutex> guard (mutex);
	stats.emplace_back (transaction_impl, is_write_a);
}

void nano::mdb_txn_tracker::erase (nano::transaction_impl * transaction_impl)
{
	std::lock_guard<std::mutex> guard (mutex);
	// clang-format off
	auto it = std::find_if (stats.begin (), stats.end (), [transaction_impl] (const auto & mdb_txn_stats) {
		return mdb_txn_stats.transaction_impl == transaction_impl;
	});
	// clang-format on
	assert (it != stats.cend ());
	output_finished (*it);
	it->timer.stop ();
	stats.erase (it);
}

nano::mdb_txn_stats::mdb_txn_stats (nano::transaction_impl * transaction_impl, bool is_write) :
transaction_impl (transaction_impl),
thread_name (nano::thread_role::get_string ()),
is_write (is_write),
stacktrace (std::make_shared<boost::stacktrace::stacktrace> ())
{
	timer.start ();
}
