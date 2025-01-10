#pragma once

#include <celerix/lib/logging_enums.hpp>
#include <celerix/lib/object_stream.hpp>
#include <celerix/lib/object_stream_adapters.hpp>
#include <celerix/lib/tomlconfig.hpp>

#include <initializer_list>
#include <memory>
#include <shared_mutex>
#include <sstream>

#include <fmt/ostream.h>
#include <spdlog/spdlog.h>

namespace celerix::log
{
template <class T>
struct arg
{
	std::string_view name;
	T const & value;

	arg (std::string_view name_a, T const & value_a) :
		name{ name_a },
		value{ value_a }
	{
	}
};

using logger_id = std::pair<celerix::log::type, celerix::log::detail>;

std::string to_string (logger_id);
logger_id parse_logger_id (std::string const &);
}

// Time helpers
namespace celerix::log
{
template <class Clock>
auto microseconds (std::chrono::time_point<Clock> time)
{
	return std::chrono::duration_cast<std::chrono::microseconds> (time.time_since_epoch ()).count ();
}

template <class Duration>
auto microseconds (Duration duration)
{
	return std::chrono::duration_cast<std::chrono::microseconds> (duration).count ();
}

template <class Clock>
auto milliseconds (std::chrono::time_point<Clock> time)
{
	return std::chrono::duration_cast<std::chrono::milliseconds> (time.time_since_epoch ()).count ();
}

template <class Duration>
auto milliseconds (Duration duration)
{
	return std::chrono::duration_cast<std::chrono::milliseconds> (duration).count ();
}

template <class Clock>
auto seconds (std::chrono::time_point<Clock> time)
{
	return std::chrono::duration_cast<std::chrono::seconds> (time.time_since_epoch ()).count ();
}

template <class Duration>
auto seconds (Duration duration)
{
	return std::chrono::duration_cast<std::chrono::seconds> (duration).count ();
}

template <class Clock>
auto milliseconds_delta (std::chrono::time_point<Clock> time, std::chrono::time_point<Clock> now = Clock::now ())
{
	return std::chrono::duration_cast<std::chrono::milliseconds> (now - time).count ();
}

template <class Clock>
auto seconds_delta (std::chrono::time_point<Clock> time, std::chrono::time_point<Clock> now = Clock::now ())
{
	return std::chrono::duration_cast<std::chrono::seconds> (now - time).count ();
}
}

namespace celerix
{
consteval bool is_tracing_enabled ()
{
#ifdef CELERIX_TRACING
	return true;
#else
	return false;
#endif
}

class log_config final
{
public:
	celerix::error serialize_toml (celerix::tomlconfig &) const;
	celerix::error deserialize_toml (celerix::tomlconfig &);

private:
	void serialize (celerix::tomlconfig &) const;
	void deserialize (celerix::tomlconfig &);

public:
	celerix::log::level default_level{ celerix::log::level::info };
	celerix::log::level flush_level{ celerix::log::level::error };

	std::map<celerix::log::logger_id, celerix::log::level> levels;

	struct console_config
	{
		bool enable{ true };
		bool colors{ true };
		bool to_cerr{ false };
	};

	struct file_config
	{
		bool enable{ true };
		std::size_t max_size{ 32 * 1024 * 1024 };
		std::size_t rotation_count{ 4 };
	};

	console_config console;
	file_config file;

	celerix::log::tracing_format tracing_format{ celerix::log::tracing_format::standard };

public: // Predefined defaults
	static log_config cli_default ();
	static log_config daemon_default ();
	static log_config tests_default ();
	static log_config sample_config (); // For auto-generated sample config files

private:
	/// Returns placeholder log levels for all loggers
	static std::map<celerix::log::logger_id, celerix::log::level> default_levels (celerix::log::level);
};

celerix::log_config load_log_config (celerix::log_config fallback, std::filesystem::path const & data_path, std::vector<std::string> const & config_overrides = {});

class logger final
{
public:
	explicit logger (std::string identifier = "");
	~logger ();

	// Disallow copies
	logger (logger const &) = delete;

public:
	static void initialize (celerix::log_config fallback, std::optional<std::filesystem::path> data_path = std::nullopt, std::vector<std::string> const & config_overrides = {});
	static void initialize_for_tests (celerix::log_config fallback);
	static void flush ();

private:
	static bool global_initialized;
	static celerix::log_config global_config;
	static std::vector<spdlog::sink_ptr> global_sinks;
	static std::function<std::string (celerix::log::logger_id, std::string identifier)> global_name_formatter;
	static celerix::object_stream_config global_tracing_config;

	static void initialize_common (celerix::log_config const &, std::optional<std::filesystem::path> data_path);

public:
	template <class... Args>
	void log (celerix::log::level level, celerix::log::type type, spdlog::format_string_t<Args...> fmt, Args &&... args)
	{
		get_logger (type).log (to_spdlog_level (level), fmt, std::forward<Args> (args)...);
	}

	template <class... Args>
	void debug (celerix::log::type type, spdlog::format_string_t<Args...> fmt, Args &&... args)
	{
		get_logger (type).debug (fmt, std::forward<Args> (args)...);
	}

	template <class... Args>
	void info (celerix::log::type type, spdlog::format_string_t<Args...> fmt, Args &&... args)
	{
		get_logger (type).info (fmt, std::forward<Args> (args)...);
	}

	template <class... Args>
	void warn (celerix::log::type type, spdlog::format_string_t<Args...> fmt, Args &&... args)
	{
		get_logger (type).warn (fmt, std::forward<Args> (args)...);
	}

	template <class... Args>
	void error (celerix::log::type type, spdlog::format_string_t<Args...> fmt, Args &&... args)
	{
		get_logger (type).error (fmt, std::forward<Args> (args)...);
	}

	template <class... Args>
	void critical (celerix::log::type type, spdlog::format_string_t<Args...> fmt, Args &&... args)
	{
		get_logger (type).critical (fmt, std::forward<Args> (args)...);
	}

public:
	template <typename... Args>
	void trace (celerix::log::type type, celerix::log::detail detail, Args &&... args)
	{
		if constexpr (is_tracing_enabled ())
		{
			debug_assert (detail != celerix::log::detail::all);

			// Include info about precise time of the event
			auto now = std::chrono::high_resolution_clock::now ();

			// TODO: Improve code indentation config
			auto & logger = get_logger (type, detail);
			logger.trace ("{}",
			celerix::streamed_args (global_tracing_config,
			celerix::log::arg{ "event", event_formatter{ type, detail } },
			celerix::log::arg{ "time", celerix::log::microseconds (now) },
			std::forward<Args> (args)...));
		}
	}

private:
	struct event_formatter final
	{
		celerix::log::type type;
		celerix::log::detail detail;

		friend std::ostream & operator<< (std::ostream & os, event_formatter const & self)
		{
			return os << to_string (self.type) << "::" << to_string (self.detail);
		}
	};

private:
	const std::string identifier;

	std::map<celerix::log::logger_id, std::shared_ptr<spdlog::logger>> spd_loggers;
	std::shared_mutex mutex;

private:
	spdlog::logger & get_logger (celerix::log::type, celerix::log::detail = celerix::log::detail::all);
	std::shared_ptr<spdlog::logger> make_logger (celerix::log::logger_id);
	celerix::log::level find_level (celerix::log::logger_id) const;

	static spdlog::level::level_enum to_spdlog_level (celerix::log::level);
};

/**
 * Returns a logger instance that can be used before node specific logging is available.
 * Should only be used for logging that happens during startup and initialization, since it won't contain node specific identifier.
 */
celerix::logger & default_logger ();
}