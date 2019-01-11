#pragma once

#include <atomic>
#include <boost/circular_buffer.hpp>
#include <boost/property_tree/ptree.hpp>
#include <chrono>
#include <map>
#include <memory>
#include <nano/lib/utility.hpp>
#include <string>
#include <unordered_map>

namespace nano
{
class node;

/**
 * Serialize and deserialize the 'statistics' node from config.json
 * All configuration values have defaults. In particular, file logging of statistics
 * is disabled by default.
 */
class stat_config
{
public:
	/** Reads the JSON statistics node */
	bool deserialize_json (boost::property_tree::ptree & tree_a);

	/** If true, sampling of counters is enabled */
	bool sampling_enabled{ false };

	/** How many sample intervals to keep in the ring buffer */
	size_t capacity{ 0 };

	/** Sample interval in milliseconds */
	size_t interval{ 0 };

	/** How often to log sample array, in milliseconds. Default is 0 (no logging) */
	size_t log_interval_samples{ 0 };

	/** How often to log counters, in milliseconds. Default is 0 (no logging) */
	size_t log_interval_counters{ 0 };

	/** Maximum number of log outputs before rotating the file */
	size_t log_rotation_count{ 100 };

	/** If true, write headers on each counter or samples writeout. The header contains log type and the current wall time. */
	bool log_headers{ true };

	/** Filename for the counter log  */
	std::string log_counters_filename{ "counters.stat" };

	/** Filename for the sampling log */
	std::string log_samples_filename{ "samples.stat" };
};

/** Value and wall time of measurement */
class stat_datapoint
{
public:
	/** Value of the sample interval */
	uint64_t value{ 0 };
	/** When the sample was added. This is wall time (system_clock), suitable for display purposes. */
	std::chrono::system_clock::time_point timestamp{ std::chrono::system_clock::now () };

	/** Add \addend to the current value and optionally update the timestamp */
	inline void add (uint64_t addend_a, bool update_timestamp_a = true)
	{
		value += addend_a;
		if (update_timestamp_a)
		{
			timestamp = std::chrono::system_clock::now ();
		}
	}
};

/** Bookkeeping of statistics for a specific type/detail/direction combination */
class stat_entry
{
public:
	stat_entry (size_t capacity_a, size_t interval_a) :
	samples (capacity_a), sample_interval (interval_a)
	{
	}

	/** Optional samples. Note that this doesn't allocate any memory unless sampling is configured, which sets the capacity. */
	boost::circular_buffer<stat_datapoint> samples;

	/** Start time of current sample interval. This is a steady clock for measuring interval; the datapoint contains the wall time. */
	std::chrono::steady_clock::time_point sample_start_time{ std::chrono::steady_clock::now () };

	/** Sample interval in milliseconds. If 0, sampling is disabled. */
	size_t sample_interval;

	/** Value within the current sample interval */
	stat_datapoint sample_current;

	/** Counting value for this entry, including the time of last update. This is never reset and only increases. */
	stat_datapoint counter;

	/** Zero or more observers for samples. Called at the end of the sample interval. */
	nano::observer_set<boost::circular_buffer<stat_datapoint> &> sample_observers;

	/** Observers for count. Called on each update. */
	nano::observer_set<uint64_t, uint64_t> count_observers;
};

/** Log sink interface */
class stat_log_sink
{
public:
	virtual ~stat_log_sink () = default;

	/** Returns a reference to the log output stream */
	virtual std::ostream & out () = 0;

	/** Called before logging starts */
	virtual void begin ()
	{
	}

	/** Called after logging is completed */
	virtual void finalize ()
	{
	}

	/** Write a header enrty to the log */
	virtual void write_header (std::string header_a, std::chrono::system_clock::time_point & walltime_a)
	{
	}

	/** Write a counter or sampling entry to the log */
	virtual void write_entry (tm & tm_a, std::string type_a, std::string detail_a, std::string dir_a, uint64_t value_a)
	{
	}

	/** Rotates the log (e.g. empty file). This is a no-op for sinks where rotation is not supported. */
	virtual void rotate ()
	{
	}

	/** Returns a reference to the log entry counter */
	inline size_t & entries ()
	{
		return log_entries;
	}

	/** Returns the string representation of the log. If not supported, an empty string is returned. */
	virtual std::string to_string ()
	{
		return "";
	}

	/**
	 * Returns the object representation of the log result. The type depends on the sink used.
	 * @returns Object, or nullptr if no object result is available.
	 */
	virtual void * to_object ()
	{
		return nullptr;
	}

protected:
	std::string tm_to_string (tm & tm);
	size_t log_entries{ 0 };
};

/**
 * Collects counts and samples for inbound and outbound traffic, blocks, errors, and so on.
 * Stats can be queried and observed on a type level (such as message and ledger) as well as a more
 * specific detail level (such as send blocks)
 */
class stat
{
public:
	/** Primary statistics type */
	enum class type : uint8_t
	{
		traffic,
		traffic_bootstrap,
		error,
		message,
		block,
		ledger,
		rollback,
		bootstrap,
		vote,
		http_callback,
		peering,
		udp
	};

	/** Optional detail type */
	enum class detail : uint8_t
	{
		all = 0,

		// error specific
		bad_sender,
		insufficient_work,
		http_callback,
		unreachable_host,

		// ledger, block, bootstrap
		send,
		receive,
		open,
		change,
		state_block,
		epoch_block,

		// message specific
		keepalive,
		publish,
		republish_vote,
		confirm_req,
		confirm_ack,
		node_id_handshake,

		// bootstrap, callback
		initiate,
		initiate_lazy,

		// bootstrap specific
		bulk_pull,
		bulk_push,
		bulk_pull_account,
		frontier_req,

		// vote specific
		vote_valid,
		vote_replay,
		vote_invalid,
		vote_overflow,

		// udp
		blocking,
		overflow,
		invalid_magic,
		invalid_network,
		invalid_header,
		invalid_message_type,
		invalid_keepalive_message,
		invalid_publish_message,
		invalid_confirm_req_message,
		invalid_confirm_ack_message,
		invalid_node_id_handshake_message,
		outdated_version,

		// peering
		handshake,
	};

	/** Direction of the stat. If the direction is irrelevant, use in */
	enum class dir : uint8_t
	{
		in,
		out
	};

	/** Constructor using the default config values */
	stat ()
	{
	}

	/**
	 * Initialize stats with a config.
	 * @param config Configuration object; deserialized from config.json
	 */
	stat (nano::stat_config config_a);

	/**
	 * Call this to override the default sample interval and capacity, for a specific stat entry.
	 * This must be called before any stat entries are added, as part of the node initialiation.
	 */
	inline void configure (stat::type type_a, stat::detail detail_a, stat::dir dir_a, size_t interval_a, size_t capacity_a)
	{
		get_entry (key_of (type_a, detail_a, dir_a), interval_a, capacity_a);
	}

	/**
	 * Disables sampling for a given type/detail/dir combination
	 */
	inline void disable_sampling (stat::type type_a, stat::detail detail_a, stat::dir dir_a)
	{
		auto entry = get_entry (key_of (type_a, detail_a, dir_a));
		entry->sample_interval = 0;
	}

	/** Increments the given counter */
	inline void inc (stat::type type_a, stat::dir dir_a = stat::dir::in)
	{
		add (type_a, dir_a, 1);
	}

	/** Increments the counter for \detail, but doesn't update at the type level */
	inline void inc_detail_only (stat::type type_a, stat::detail detail_a, stat::dir dir_a = stat::dir::in)
	{
		add (type_a, detail_a, dir_a, 1);
	}

	/** Increments the given counter */
	inline void inc (stat::type type_a, stat::detail detail_a, stat::dir dir_a = stat::dir::in)
	{
		add (type_a, detail_a, dir_a, 1);
	}

	/** Adds \p value to the given counter */
	inline void add (stat::type type_a, stat::dir dir_a, uint64_t value_a)
	{
		add (type_a, detail::all, dir_a, value_a);
	}

	/**
	 * Add \p value to stat. If sampling is configured, this will update the current sample and
	 * call any sample observers if the interval is over.
	 *
	 * @param type Main statistics type
	 * @param detail Detail type, or detail::none to register on type-level only
	 * @param dir Direction
	 * @param value The amount to add
	 * @param detail_only If true, only update the detail-level counter
	 */
	inline void add (stat::type type_a, stat::detail detail_a, stat::dir dir_a, uint64_t value_a, bool detail_only_a = false)
	{
		constexpr uint32_t no_detail_mask = 0xffff00ff;
		uint32_t key = key_of (type_a, detail_a, dir_a);

		update (key, value_a);

		// Optionally update at type-level as well
		if (!detail_only_a && (key & no_detail_mask) != key)
		{
			update (key & no_detail_mask, value_a);
		}
	}

	/**
	 * Add a sampling observer for a given counter.
	 * The observer receives a snapshot of the current sampling. Accessing the sample buffer is thus thread safe.
	 * To avoid recursion, the observer callback must only use the received data point snapshop, not query the stat object.
	 * @param observer The observer receives a snapshot of the current samples.
	 */
	inline void observe_sample (stat::type type_a, stat::detail detail_a, stat::dir dir_a, std::function<void(boost::circular_buffer<stat_datapoint> &)> observer_a)
	{
		get_entry (key_of (type_a, detail_a, dir_a))->sample_observers.add (observer_a);
	}

	inline void observe_sample (stat::type type_a, stat::dir dir_a, std::function<void(boost::circular_buffer<stat_datapoint> &)> observer_a)
	{
		observe_sample (type_a, stat::detail::all, dir_a, observer_a);
	}

	/**
	 * Add count observer for a given type, detail and direction combination. The observer receives old and new value.
	 * To avoid recursion, the observer callback must only use the received counts, not query the stat object.
	 * @param observer The observer receives the old and the new count.
	 */
	inline void observe_count (stat::type type_a, stat::detail detail_a, stat::dir dir_a, std::function<void(uint64_t, uint64_t)> observer_a)
	{
		get_entry (key_of (type_a, detail_a, dir_a))->count_observers.add (observer_a);
	}

	/** Returns a potentially empty list of the last N samples, where N is determined by the 'capacity' configuration */
	inline boost::circular_buffer<stat_datapoint> * samples (stat::type type_a, stat::detail detail_a, stat::dir dir_a)
	{
		return &get_entry (key_of (type_a, detail_a, dir_a))->samples;
	}

	/** Returns current value for the given counter at the type level */
	inline uint64_t count (stat::type type_a, stat::dir dir_a = stat::dir::in)
	{
		return count (type_a, stat::detail::all, dir_a);
	}

	/** Returns current value for the given counter at the detail level */
	inline uint64_t count (stat::type type_a, stat::detail detail_a, stat::dir dir_a = stat::dir::in)
	{
		return get_entry (key_of (type_a, detail_a, dir_a))->counter.value;
	}

	/** Log counters to the given log link */
	void log_counters (stat_log_sink & sink_a);

	/** Log samples to the given log sink */
	void log_samples (stat_log_sink & sink_a);

	/** Returns a new JSON log sink */
	std::unique_ptr<stat_log_sink> log_sink_json ();

	/** Returns a new file log sink */
	std::unique_ptr<stat_log_sink> log_sink_file (std::string filename_a);

private:
	static std::string type_to_string (uint32_t key_a);
	static std::string detail_to_string (uint32_t key_a);
	static std::string dir_to_string (uint32_t key_a);

	/** Constructs a key given type, detail and direction. This is used as input to update(...) and get_entry(...) */
	inline uint32_t key_of (stat::type type_a, stat::detail detail_a, stat::dir dir_a) const
	{
		return static_cast<uint8_t> (type_a) << 16 | static_cast<uint8_t> (detail_a) << 8 | static_cast<uint8_t> (dir_a);
	}

	/** Get entry for key, creating a new entry if necessary, using interval and sample count from config */
	std::shared_ptr<nano::stat_entry> get_entry (uint32_t key_a);

	/** Get entry for key, creating a new entry if necessary */
	std::shared_ptr<nano::stat_entry> get_entry (uint32_t key_a, size_t sample_interval_a, size_t max_samples_a);

	/** Unlocked implementation of get_entry() */
	std::shared_ptr<nano::stat_entry> get_entry_impl (uint32_t key_a, size_t sample_interval_a, size_t max_samples_a);

	/**
	 * Update count and sample and call any observers on the key
	 * @param key a key constructor from stat::type, stat::detail and stat::direction
	 * @value Amount to add to the counter
	 */
	void update (uint32_t key_a, uint64_t value_a);

	/** Unlocked implementation of log_counters() to avoid using recursive locking */
	void log_counters_impl (stat_log_sink & sink_a);

	/** Unlocked implementation of log_samples() to avoid using recursive locking */
	void log_samples_impl (stat_log_sink & sink_a);

	/** Configuration deserialized from config.json */
	nano::stat_config config;

	/** Stat entries are sorted by key to simplify processing of log output */
	std::map<uint32_t, std::shared_ptr<nano::stat_entry>> entries;
	std::chrono::steady_clock::time_point log_last_count_writeout{ std::chrono::steady_clock::now () };
	std::chrono::steady_clock::time_point log_last_sample_writeout{ std::chrono::steady_clock::now () };

	/** All access to stat is thread safe, including calls from observers on the same thread */
	std::mutex stat_mutex;
};
}
