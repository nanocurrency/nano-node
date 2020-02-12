#include <nano/node/lmdb/lmdb_env.hpp>

#include <boost/filesystem/operations.hpp>

nano::mdb_env::mdb_env (bool & error_a, boost::filesystem::path const & path_a, int max_dbs_a, bool use_no_mem_init_a, size_t map_size_a)
{
	init (error_a, path_a, max_dbs_a, use_no_mem_init_a, map_size_a);
}

void nano::mdb_env::init (bool & error_a, boost::filesystem::path const & path_a, int max_dbs_a, bool use_no_mem_init_a, size_t map_size_a)
{
	boost::system::error_code error_mkdir, error_chmod;
	if (path_a.has_parent_path ())
	{
		boost::filesystem::create_directories (path_a.parent_path (), error_mkdir);
		nano::set_secure_perm_directory (path_a.parent_path (), error_chmod);
		if (!error_mkdir)
		{
			auto status1 (mdb_env_create (&environment));
			release_assert (status1 == 0);
			auto status2 (mdb_env_set_maxdbs (environment, max_dbs_a));
			release_assert (status2 == 0);
			auto map_size = map_size_a;
			auto max_valgrind_map_size = 16 * 1024 * 1024;
			if (running_within_valgrind () && map_size_a > max_valgrind_map_size)
			{
				// In order to run LMDB under Valgrind, the maximum map size must be smaller than half your available RAM
				map_size = max_valgrind_map_size;
			}
			auto status3 (mdb_env_set_mapsize (environment, map_size));
			release_assert (status3 == 0);
			// It seems if there's ever more threads than mdb_env_set_maxreaders has read slots available, we get failures on transaction creation unless MDB_NOTLS is specified
			// This can happen if something like 256 io_threads are specified in the node config
			// MDB_NORDAHEAD will allow platforms that support it to load the DB in memory as needed.
			// MDB_NOMEMINIT prevents zeroing malloc'ed pages. Can provide improvement for non-sensitive data but may make memory checkers noisy (e.g valgrind).
			auto environment_flags = MDB_NOSUBDIR | MDB_NOTLS | MDB_NORDAHEAD;
			if (!running_within_valgrind () && use_no_mem_init_a)
			{
				environment_flags |= MDB_NOMEMINIT;
			}
			auto status4 (mdb_env_open (environment, path_a.string ().c_str (), environment_flags, 00600));
			if (status4 != 0)
			{
				std::cerr << "Could not open lmdb environment: " << status4;
				char * error_str (mdb_strerror (status4));
				if (error_str)
				{
					std::cerr << ", " << error_str;
				}
				std::cerr << std::endl;
			}
			release_assert (status4 == 0);
			error_a = status4 != 0;
		}
		else
		{
			error_a = true;
			environment = nullptr;
		}
	}
	else
	{
		error_a = true;
		environment = nullptr;
	}
}

nano::mdb_env::~mdb_env ()
{
	if (environment != nullptr)
	{
		mdb_env_close (environment);
	}
}

nano::mdb_env::operator MDB_env * () const
{
	return environment;
}

nano::read_transaction nano::mdb_env::tx_begin_read (mdb_txn_callbacks mdb_txn_callbacks) const
{
	return nano::read_transaction{ std::make_unique<nano::read_mdb_txn> (*this, mdb_txn_callbacks) };
}

nano::write_transaction nano::mdb_env::tx_begin_write (mdb_txn_callbacks mdb_txn_callbacks) const
{
	return nano::write_transaction{ std::make_unique<nano::write_mdb_txn> (*this, mdb_txn_callbacks) };
}

MDB_txn * nano::mdb_env::tx (nano::transaction const & transaction_a) const
{
	return static_cast<MDB_txn *> (transaction_a.get_handle ());
}

/*LMDB TRACE START*/

#ifdef __clang__
#define BOOST_STACKTRACE_GNU_SOURCE_NOT_REQUIRED 1
#endif
#include <boost/stacktrace.hpp>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <mutex>
namespace
{
enum mdb_op_type
{
	get,
	put,
	del
};

/** Stats for read/write operations. Note that we segreggate writes and deletes. */
struct nano_mdb_stat
{
	uint64_t invocations[3]{ 0 };
	uint64_t bytes[3]{ 0 };
	uint64_t bytes_total () const
	{
		return bytes[0] + bytes[1] + bytes[2];
	}
	uint64_t invocations_total () const
	{
		return invocations[0] + invocations[1] + invocations[2];
	}
};

struct nano_mdb_stat_tx : public nano_mdb_stat
{
	size_t txid{ 0 };
	bool active{ true };
	std::chrono::steady_clock::time_point start{ std::chrono::steady_clock::now () };
	std::chrono::steady_clock::time_point end;
	size_t trace_hash{ 0 };
	std::string duration_string () const
	{
		std::ostringstream ostr;
		ostr << std::fixed << std::setprecision (2) << std::chrono::duration_cast<std::chrono::duration<double>> (end - start).count () << "s";
		return ostr.str ();
	}
};

std::map<size_t, nano_mdb_stat_tx> stats_by_tx;
std::map<size_t, std::string> traces_by_tx;
std::map<MDB_dbi, std::string> table_names;
std::map<size_t, std::string> traces_by_hash;
std::map<size_t, nano_mdb_stat> stats_by_hash;
std::map<MDB_dbi, nano_mdb_stat> stats_by_dbi;
std::map<size_t, uint64_t> page_flush_by_hash;
std::mutex mutex_traces_by_tx;
std::mutex mutex_traces_by_hash;
std::mutex mutex_traces_log;
std::mutex mutex_stats_by_hash;
std::mutex mutex_stats_by_tx;
std::mutex mutex_stats_by_dbi;
std::mutex mutex_page_flush_by_hash;

std::string format_xfer (size_t bytes)
{
	static const std::vector<std::string> units = { " b", " KB", " MB", " GB", " TB", " PB" };
	auto index = bytes == 0 ? 0 : std::min (units.size () - 1, static_cast<size_t> (std::floor (std::log2 (bytes) / 10)));
	std::string unit = units[index];
	bytes /= std::pow (1024, index);
	return std::to_string (bytes) + unit;
}

std::string format_count (size_t bytes)
{
	static const std::vector<std::string> units = { "", "K", "M", "B", "T" };
	auto index = bytes == 0 ? 0 : std::min (units.size () - 1, static_cast<size_t> (std::floor (std::log2 (bytes) / 10)));
	std::string unit = units[index];
	bytes /= std::pow (1000, index);
	return std::to_string (bytes) + unit;
}

} //ns

extern "C" {
size_t nano_profile_next_id ()
{
	static std::atomic<size_t> id;
	return id.fetch_add (1);
}

void nano_profile_register_db (MDB_dbi dbi, const char * name)
{
	table_names[dbi] = name;
	std::cout << dbi << " = " << name << std::endl;
}

static std::ostringstream format_trace (const boost::stacktrace::basic_stacktrace<std::allocator<boost::stacktrace::frame>> & trace)
{
	std::ostringstream str;
	for (boost::stacktrace::frame frame : trace)
	{
		auto name (frame.name ());
		auto prefix (name.find ("nano"));
		if (prefix == 0)
		{
			name.erase (0, 6);
		}
		// Loop to handle (anonymous)
		for (;;)
		{
			auto start (name.find_first_of ('('));
			auto end (name.find_first_of (')'));
			if (start != std::string::npos && end != std::string::npos)
			{
				name.erase (start, (end - start) + 1);
			}
			else
				break;
		}

		str << name << " <= ";
	}
	return str;
}

void nano_profile_tx_begin (size_t tx)
{
	auto trace = boost::stacktrace::stacktrace (5, 4);
	size_t trace_hash = boost::stacktrace::hash_value (trace);
	{
		std::lock_guard<std::mutex> lock (mutex_traces_by_hash);
		if (traces_by_hash.find (trace_hash) == traces_by_hash.end ())
		{
			std::ostringstream str = format_trace (trace);
			traces_by_hash[trace_hash] = str.str ();
		}
	}

	std::lock_guard<std::mutex> lock (mutex_stats_by_tx);
	stats_by_tx[tx].txid = tx;
	stats_by_tx[tx].trace_hash = trace_hash;
}

void nano_profile_tx_commit (size_t tx)
{
	std::lock_guard<std::mutex> lock (mutex_stats_by_tx);
	stats_by_tx[tx].active = false;
	stats_by_tx[tx].end = std::chrono::steady_clock::now ();
}

/** Called when mdb flushes N pages. This accounts for skipped pages if using MDB_WRITEMAP. */
void nano_profile_page_flush_mdb (int page_count)
{
	auto trace = boost::stacktrace::stacktrace (8, 2);
	size_t trace_hash = boost::stacktrace::hash_value (trace);
	{
		std::lock_guard<std::mutex> lock (mutex_traces_by_hash);
		if (traces_by_hash.find (trace_hash) == traces_by_hash.end ())
		{
			traces_by_hash[trace_hash] = format_trace (trace).str ();
		}
	}
	std::lock_guard<std::mutex> lock (mutex_page_flush_by_hash);
	page_flush_by_hash[trace_hash] += page_count;
}

/** Called by mdb_get/put/del. We collect stats and stacktraces to figure out where the IO overhead is. */
void nano_profile_mdb (size_t tx, int op_type /*0=get,1=put,2=del*/, MDB_dbi dbi, size_t size)
{
	static long counter = 0;
	counter++;

	auto trace = boost::stacktrace::stacktrace (2, 6);
	size_t trace_hash = boost::stacktrace::hash_value (trace);
	{
		std::lock_guard<std::mutex> lock (mutex_traces_by_hash);
		if (traces_by_hash.find (trace_hash) == traces_by_hash.end ())
		{
			traces_by_hash[trace_hash] = format_trace (trace).str ();
		}
	}

	{
		std::lock_guard<std::mutex> lock (mutex_stats_by_hash);
		stats_by_hash[trace_hash].bytes[op_type] += size;
		stats_by_hash[trace_hash].invocations[op_type]++;
	}

	{
		std::lock_guard<std::mutex> lock (mutex_stats_by_dbi);
		stats_by_dbi[dbi].bytes[op_type] += size;
		stats_by_dbi[dbi].invocations[op_type]++;
	}

	{
		std::lock_guard<std::mutex> lock (mutex_stats_by_tx);
		stats_by_tx[tx].bytes[op_type] += size;
		stats_by_tx[tx].invocations[op_type]++;
	}

	static std::chrono::steady_clock::time_point start{ std::chrono::steady_clock::now () };
	static std::chrono::steady_clock::time_point last_dynamic_clear{ std::chrono::steady_clock::now () };
	auto steady_now (std::chrono::steady_clock::now ());

	double trace_freq = 5;
	const char * trace_freq_str = std::getenv ("LMDB_TRACE_FREQ");
	if (trace_freq_str)
	{
		trace_freq = std::stod (trace_freq_str);
	}

	if (std::chrono::duration_cast<std::chrono::seconds> (steady_now - start).count () >= trace_freq)
	{
		// Read env vars on each dump to allow changing them at runtime via gdb
		const char * trace_dyn_tx = std::getenv ("LMDB_TRACE_TX_DYNAMIC");
		const char * trace_tx = std::getenv ("LMDB_TRACE_TX");
		const char * trace_collate = std::getenv ("LMDB_TRACE_TX_COLLATE");
		const char * trace_dbi = std::getenv ("LMDB_TRACE_DBI");

		std::lock_guard<std::mutex> lock (mutex_traces_log);
		start = steady_now;
		auto now = std::chrono::system_clock::now ();
		std::time_t walltime = std::chrono::system_clock::to_time_t (now);
		std::cout << std::endl
		          << "LMDB I/O trace " << std::put_time (std::localtime (&walltime), "%X") << std::endl;

		// Per-table stats
		if (trace_dbi)
		{
			std::lock_guard<std::mutex> lock (mutex_stats_by_dbi);
			for (auto & stat : stats_by_dbi)
			{
				std::cout << std::setw (15) << table_names[stat.first].substr (0, 14) << ": ";
				std::cout << std::setw (6) << format_count (stat.second.invocations[mdb_op_type::get]) << " r (" << std::setw (6) << format_xfer (stat.second.bytes[mdb_op_type::get]) << "), ";
				std::cout << std::setw (6) << format_count (stat.second.invocations[mdb_op_type::put]) << " w (" << std::setw (6) << format_xfer (stat.second.bytes[mdb_op_type::put]) << "), ";
				std::cout << std::setw (6) << format_count (stat.second.invocations[mdb_op_type::del]) << " d (" << std::setw (6) << format_xfer (stat.second.bytes[mdb_op_type::del]) << ")";
				std::cout << std::endl;
			}
		}

		// Large transactions
		if (trace_tx || trace_dyn_tx)
		{
			if (trace_tx)
			{
				std::cout << std::endl
				          << "Heaviest transactions since start";
			}
			else
			{
				std::cout << std::endl
				          << "Heaviest transactions past " << trace_dyn_tx << " seconds";
			}

			if (trace_collate)
			{
				std::cout << " (collated)";
			}
			std::cout << ":" << std::endl;

			std::lock_guard<std::mutex> lock (mutex_stats_by_tx);
			typedef std::function<bool(std::pair<size_t, nano_mdb_stat_tx>, std::pair<size_t, nano_mdb_stat_tx>)> Comparator;
			Comparator compFunctor = [](std::pair<size_t, nano_mdb_stat_tx> elem1, std::pair<size_t, nano_mdb_stat_tx> elem2) {
				return elem1.second.bytes_total () > elem2.second.bytes_total ();
			};
			std::set<std::pair<size_t, nano_mdb_stat_tx>, Comparator> tx_set (stats_by_tx.begin (), stats_by_tx.end (), compFunctor);

			long trace_count = 10;
			const char * trace_count_str = std::getenv ("LMDB_TRACE_TX_COUNT");
			if (trace_count_str)
			{
				trace_count = std::stol (trace_count_str);
			}

			long traces = 0;
			std::set<size_t> trace_hashes_seen;
			for (auto & stat : tx_set)
			{
				bool seen (trace_hashes_seen.find (stat.second.trace_hash) != trace_hashes_seen.end ());
				if (!stat.second.active && (!trace_collate || !seen))
				{
					trace_hashes_seen.insert (stat.second.trace_hash);
					std::cout << "  tx#" << stat.first << ": " << std::dec;
					std::cout << stat.second.invocations[mdb_op_type::get] << " r (" << format_xfer (stat.second.bytes[mdb_op_type::get]) << "), ";
					std::cout << stat.second.invocations[mdb_op_type::put] << " w (" << format_xfer (stat.second.bytes[mdb_op_type::put]) << "), ";
					std::cout << stat.second.invocations[mdb_op_type::del] << " d (" << format_xfer (stat.second.bytes[mdb_op_type::del]) << "), ";
					std::cout << stat.second.invocations_total () << " tot (" << std::setw (6) << format_xfer (stat.second.bytes_total ()) << ")";
					std::cout << std::endl;

					std::lock_guard<std::mutex> lock (mutex_traces_by_hash);
					auto match (traces_by_hash.find (stat.second.trace_hash));
					if (match != traces_by_hash.end ())
					{
						std::cout << "    " << stat.second.duration_string () << " - " << match->second << std::endl
						          << std::endl;
					}

					if (++traces >= trace_count)
					{
						break;
					}
				}
			}

			if (trace_dyn_tx)
			{
				if (std::chrono::duration_cast<std::chrono::seconds> (steady_now - last_dynamic_clear).count () > std::stol (trace_dyn_tx))
				{
					last_dynamic_clear = steady_now;
					stats_by_tx.clear ();
				}
			}
			else if (stats_by_tx.size () > 10000)
			{
				// Keep top 500 transactions for future dumps
				stats_by_tx.clear ();
				std::copy_n (tx_set.begin (), std::min (tx_set.size (), size_t (500)), std::inserter (stats_by_tx, stats_by_tx.end ()));
			}
		}

		// per backtrace
		if (std::getenv ("LMDB_TRACE_BACKTRACES"))
		{
			std::lock_guard<std::mutex> lock (mutex_stats_by_hash);
			std::cout << "Unique backtraces: " << stats_by_hash.size () << std::endl;
		}

		// page flushes per backtrace
		if (std::getenv ("LMDB_TRACE_PAGEFLUSHES"))
		{
			std::cout << "Total page flushes per backtrace: " << std::endl;
			std::lock_guard<std::mutex> lock (mutex_page_flush_by_hash);
			for (auto & flushes : page_flush_by_hash)
			{
				std::cout << "  " << std::setw (8) << std::hex << flushes.first << ": " << std::dec << flushes.second << " pages (" << format_xfer (flushes.second * 4096) << "): ";
				std::lock_guard<std::mutex> lock (mutex_traces_by_hash);
				std::cout << traces_by_hash[flushes.first] << std::endl;
			}
		}

		std::cout << "=end of stat=" << std::endl;
	}
}
}

/*LMDB TRACE END*/
