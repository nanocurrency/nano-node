#include <nano/boost/stacktrace.hpp>
#include <nano/lib/jsonconfig.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/lib/tomlconfig.hpp>
#include <nano/lib/utility.hpp>
#include <nano/store/txn_tracking.hpp>

#include <boost/format.hpp>

namespace
{
class matches_txn final
{
public:
	explicit matches_txn (nano::store::transaction_impl const * txn_impl_a) :
		txn_impl (txn_impl_a)
	{
	}

	bool operator() (nano::store::txn_stats const & txn_stats)
	{
		return (txn_stats.txn_impl == txn_impl);
	}

private:
	nano::store::transaction_impl const * txn_impl;
};
}

/*
 * txn_stats
 */

nano::store::txn_stats::txn_stats (transaction_impl const * txn_impl_a) :
	txn_impl (txn_impl_a),
	thread_name (nano::thread_role::get_string ()),
	stacktrace (std::make_shared<boost::stacktrace::stacktrace> ())
{
	timer.start ();
}

bool nano::store::txn_stats::is_write () const
{
	return (dynamic_cast<write_transaction_impl const *> (txn_impl) != nullptr);
}

/*
 * txn_tracker
 */

nano::store::txn_tracker::txn_tracker (nano::logger & logger_a, txn_tracking_config const & config_a) :
	logger (logger_a),
	config (config_a)
{
}

void nano::store::txn_tracker::serialize_json (boost::property_tree::ptree & json, std::chrono::milliseconds min_read_time, std::chrono::milliseconds min_write_time)
{
	// Copying is cheap compared to generating the stack trace strings, so reduce time holding the mutex
	std::vector<txn_stats> copy_stats;
	std::vector<bool> are_writes;
	{
		nano::lock_guard<nano::mutex> guard (mutex);
		copy_stats = stats;
		are_writes.reserve (stats.size ());
		std::transform (stats.cbegin (), stats.cend (), std::back_inserter (are_writes), [] (auto & stat) {
			return stat.is_write ();
		});
	}

	// Get the time difference now as creating stacktraces (Debug/Windows for instance) can take a while so results won't be as accurate
	std::vector<std::chrono::milliseconds> times_since_start;
	times_since_start.reserve (copy_stats.size ());
	std::transform (copy_stats.cbegin (), copy_stats.cend (), std::back_inserter (times_since_start), [] (auto const & stat) {
		return stat.timer.since_start ();
	});
	debug_assert (times_since_start.size () == copy_stats.size ());

	for (std::size_t i = 0; i < times_since_start.size (); ++i)
	{
		auto const & stat = copy_stats[i];
		auto time_held_open = times_since_start[i];

		if ((are_writes[i] && time_held_open >= min_write_time) || (!are_writes[i] && time_held_open >= min_read_time))
		{
			nano::jsonconfig lock_config;

			lock_config.put ("thread", stat.thread_name);
			lock_config.put ("time_held_open", time_held_open.count ());
			lock_config.put ("write", !!are_writes[i]);

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
			lock_config.put_child ("stacktrace", stack);
			json.push_back (std::make_pair ("", lock_config.get_tree ()));
		}
	}
}

void nano::store::txn_tracker::log_if_held_long_enough (txn_stats const & stats) const
{
	// Only log these transactions if they were held for longer than the min_read_txn_time/min_write_txn_time config values
	auto is_write = stats.is_write ();
	auto time_open = stats.timer.since_start ();

	if ((is_write && time_open >= config.min_write_txn_time) || (!is_write && time_open >= config.min_read_txn_time))
	{
		debug_assert (stats.stacktrace);

		logger.warn (nano::log::type::txn_tracker, "{}ms {} held on thread {}\n{}",
		time_open.count (),
		is_write ? "write lock" : "read",
		stats.thread_name,
		nano::util::to_str (*stats.stacktrace));
	}
}

void nano::store::txn_tracker::add (transaction_impl const * transaction_impl)
{
	nano::lock_guard<nano::mutex> guard (mutex);
	debug_assert (std::find_if (stats.cbegin (), stats.cend (), matches_txn (transaction_impl)) == stats.cend ());
	stats.emplace_back (transaction_impl);
}

void nano::store::txn_tracker::erase (transaction_impl const * transaction_impl)
{
	nano::unique_lock<nano::mutex> lk (mutex);
	auto it = std::find_if (stats.begin (), stats.end (), matches_txn (transaction_impl));
	if (it != stats.end ())
	{
		auto tracker_stats_copy = *it;
		stats.erase (it);
		lk.unlock ();
		log_if_held_long_enough (tracker_stats_copy);
	}
}

/*
 * txn_tracking_config
 */

nano::error nano::store::txn_tracking_config::serialize_toml (nano::tomlconfig & toml) const
{
	toml.put ("enable", enable, "Enable or disable database transaction tracing.\ntype:bool");
	toml.put ("min_read_txn_time", min_read_txn_time.count (), "Log stacktrace when read transactions are held longer than this duration.\ntype:milliseconds");
	toml.put ("min_write_txn_time", min_write_txn_time.count (), "Log stacktrace when write transactions are held longer than this duration.\ntype:milliseconds");
	return toml.get_error ();
}

nano::error nano::store::txn_tracking_config::deserialize_toml (nano::tomlconfig & toml)
{
	toml.get ("enable", enable);
	toml.get_duration ("min_read_txn_time", min_read_txn_time);
	toml.get_duration ("min_write_txn_time", min_write_txn_time);
	return toml.get_error ();
}