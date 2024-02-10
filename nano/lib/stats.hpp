#pragma once

#include <nano/lib/errors.hpp>
#include <nano/lib/observer_set.hpp>
#include <nano/lib/stats_enums.hpp>
#include <nano/lib/utility.hpp>

#include <boost/circular_buffer.hpp>

#include <chrono>
#include <initializer_list>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>

namespace nano
{
class node;
class tomlconfig;
class jsonconfig;

/**
 * Serialize and deserialize the 'statistics' node from config.json
 * All configuration values have defaults. In particular, file logging of statistics
 * is disabled by default.
 */
class stats_config final
{
public:
	/** Reads the JSON statistics node */
	nano::error deserialize_toml (nano::tomlconfig & toml);
	nano::error serialize_toml (nano::tomlconfig & toml) const;

public:
	/** Maximum number samples to keep in the ring buffer */
	size_t max_samples{ 1024 * 16 };

	/** How often to log sample array, in milliseconds. Default is 0 (no logging) */
	std::chrono::milliseconds log_samples_interval{ 0 };

	/** How often to log counters, in milliseconds. Default is 0 (no logging) */
	std::chrono::milliseconds log_counters_interval{ 0 };

	/** Maximum number of log outputs before rotating the file */
	size_t log_rotation_count{ 100 };

	/** If true, write headers on each counter or samples writeout. The header contains log type and the current wall time. */
	bool log_headers{ true };

	/** Filename for the counter log  */
	std::string log_counters_filename{ "counters.stat" };

	/** Filename for the sampling log */
	std::string log_samples_filename{ "samples.stat" };
};

class stat_log_sink;

/**
 * Collects counts and samples for inbound and outbound traffic, blocks, errors, and so on.
 * Stats can be queried and observed on a type level (such as message and ledger) as well as a more
 * specific detail level (such as send blocks)
 */
class stats final
{
public:
	using counter_value_t = uint64_t;
	using sampler_value_t = int64_t;

public:
	/** Constructor using the default config values */
	stats () = default;

	/**
	 * Initialize stats with a config.
	 */
	explicit stats (nano::stats_config);

	/** Stop stats being output */
	void stop ();

	/** Clear all stats */
	void clear ();

	/** Increments the given counter */
	void inc (stat::type type, stat::dir dir = stat::dir::in)
	{
		add (type, dir, 1);
	}

	/** Increments the given counter */
	void inc (stat::type type, stat::detail detail, stat::dir dir = stat::dir::in)
	{
		add (type, detail, dir, 1);
	}

	/** Adds \p value to the given counter */
	void add (stat::type type, stat::detail detail, counter_value_t value)
	{
		add (type, detail, stat::dir::in, value);
	}

	/** Adds \p value to the given counter */
	void add (stat::type type, stat::dir dir, counter_value_t value)
	{
		add (type, stat::detail::all, dir, value);
	}

	/** Adds \p value to the given counter */
	void add (stat::type type, stat::detail detail, stat::dir dir, counter_value_t value);

	/** Returns current value for the given counter at the type level */
	counter_value_t count (stat::type type, stat::dir dir = stat::dir::in) const
	{
		return count (type, stat::detail::all, dir);
	}

	/** Returns current value for the given counter at the detail level */
	counter_value_t count (stat::type type, stat::detail detail, stat::dir dir = stat::dir::in) const;

	/** Adds a sample to the given sampler */
	void sample (stat::type type, stat::sample sample, sampler_value_t value);

	/** Returns a potentially empty list of the last N samples, where N is determined by the 'max_samples' configuration. Samples are reset after each lookup. */
	std::vector<sampler_value_t> samples (stat::type type, stat::sample sample);

	/** Returns the number of seconds since clear() was last called, or node startup if it's never called. */
	std::chrono::seconds last_reset ();

	/** Log counters to the given log link */
	void log_counters (stat_log_sink & sink);

	/** Log samples to the given log sink */
	void log_samples (stat_log_sink & sink);

	/** Returns a new JSON log sink */
	std::unique_ptr<stat_log_sink> log_sink_json () const;

public:
	enum class category
	{
		counters,
		samples
	};

	/** Return string showing stats counters (convenience function for debugging) */
	std::string dump (category category = category::counters);

private:
	struct counter_key
	{
		stat::type type;
		stat::detail detail;
		stat::dir dir;

		auto operator<=> (const counter_key &) const = default;
	};

	struct sampler_key
	{
		stat::type type;
		stat::sample sample;

		auto operator<=> (const sampler_key &) const = default;
	};

private:
	class counter_entry
	{
	public:
		// Prevent copying
		counter_entry () = default;
		counter_entry (counter_entry const &) = delete;
		counter_entry & operator= (counter_entry const &) = delete;

	public:
		std::atomic<counter_value_t> value{ 0 };
	};

	class sampler_entry
	{
	public:
		// Prevent copying
		sampler_entry () = default;
		sampler_entry (sampler_entry const &) = delete;
		sampler_entry & operator= (sampler_entry const &) = delete;

	public:
		void add (sampler_value_t value, size_t max_samples);
		std::vector<sampler_value_t> collect ();

	private:
		boost::circular_buffer<sampler_value_t> samples;
		mutable nano::mutex mutex;
	};

	// Wrap in unique_ptrs because mutex members are not movable
	std::map<counter_key, std::unique_ptr<counter_entry>> counters;
	std::map<sampler_key, std::unique_ptr<sampler_entry>> samplers;

private:
	/**
	 * Update count and sample and call any observers on the key
	 * @value Amount to add to the counter
	 */
	void update ();

	/** Unlocked implementation of log_counters() to avoid using recursive locking */
	void log_counters_impl (stat_log_sink & sink, tm & tm);

	/** Unlocked implementation of log_samples() to avoid using recursive locking */
	void log_samples_impl (stat_log_sink & sink, tm & tm);

private:
	nano::stats_config const config;

	/** Time of last clear() call */
	std::chrono::steady_clock::time_point timestamp{ std::chrono::steady_clock::now () };

	std::chrono::steady_clock::time_point log_last_count_writeout{ std::chrono::steady_clock::now () };
	std::chrono::steady_clock::time_point log_last_sample_writeout{ std::chrono::steady_clock::now () };

	/** Whether stats should be output */
	bool stopped{ false };

	mutable std::shared_mutex mutex;
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
	virtual void write_header (std::string const & header, std::chrono::system_clock::time_point & walltime)
	{
	}

	/** Write a counter or sampling entry to the log. Some log sinks may support writing histograms as well. */
	virtual void write_counter_entry (tm & tm, std::string const & type, std::string const & detail, std::string const & dir, stats::counter_value_t value)
	{
	}

	virtual void write_sampler_entry (tm & tm, std::string const & type, std::string const & sample, stats::sampler_value_t value)
	{
	}

	/** Rotates the log (e.g. empty file). This is a no-op for sinks where rotation is not supported. */
	virtual void rotate ()
	{
	}

	/** Returns a reference to the log entry counter */
	std::size_t & entries ()
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
}
