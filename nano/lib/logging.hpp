#pragma once

#include <nano/lib/logging_enums.hpp>
#include <nano/lib/object_stream.hpp>
#include <nano/lib/object_stream_adapters.hpp>
#include <nano/lib/tomlconfig.hpp>

#include <initializer_list>
#include <memory>
#include <shared_mutex>
#include <sstream>

#include <fmt/ostream.h>
#include <spdlog/spdlog.h>

namespace nano::log
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

using logger_id = std::pair<nano::log::type, nano::log::detail>;

std::string to_string (logger_id);
logger_id parse_logger_id (std::string const &);

template <class Clock>
auto microseconds (std::chrono::time_point<Clock> time)
{
	return std::chrono::duration_cast<std::chrono::microseconds> (time.time_since_epoch ()).count ();
}
}

namespace nano
{
consteval bool is_tracing_enabled ()
{
#ifdef NANO_TRACING
	return true;
#else
	return false;
#endif
}

class log_config final
{
public:
	nano::error serialize_toml (nano::tomlconfig &) const;
	nano::error deserialize_toml (nano::tomlconfig &);

private:
	void serialize (nano::tomlconfig &) const;
	void deserialize (nano::tomlconfig &);

public:
	nano::log::level default_level{ nano::log::level::info };
	nano::log::level flush_level{ nano::log::level::error };

	std::map<nano::log::logger_id, nano::log::level> levels;

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

	nano::log::tracing_format tracing_format{ nano::log::tracing_format::standard };

public: // Predefined defaults
	static log_config cli_default ();
	static log_config daemon_default ();
	static log_config tests_default ();
	static log_config sample_config (); // For auto-generated sample config files

private:
	/// Returns placeholder log levels for all loggers
	static std::map<nano::log::logger_id, nano::log::level> default_levels (nano::log::level);
};

nano::log_config load_log_config (nano::log_config fallback, std::filesystem::path const & data_path, std::vector<std::string> const & config_overrides = {});

class logger final
{
public:
	explicit logger (std::string identifier = "");
	~logger ();

	// Disallow copies
	logger (logger const &) = delete;

public:
	static void initialize (nano::log_config fallback, std::optional<std::filesystem::path> data_path = std::nullopt, std::vector<std::string> const & config_overrides = {});
	static void initialize_for_tests (nano::log_config fallback);
	static void flush ();

private:
	static bool global_initialized;
	static nano::log_config global_config;
	static std::vector<spdlog::sink_ptr> global_sinks;
	static std::function<std::string (nano::log::logger_id, std::string identifier)> global_name_formatter;
	static nano::object_stream_config global_tracing_config;

	static void initialize_common (nano::log_config const &, std::optional<std::filesystem::path> data_path);

public:
	template <class... Args>
	void log (nano::log::level level, nano::log::type type, spdlog::format_string_t<Args...> fmt, Args &&... args)
	{
		get_logger (type).log (to_spdlog_level (level), fmt, std::forward<Args> (args)...);
	}

	template <class... Args>
	void debug (nano::log::type type, spdlog::format_string_t<Args...> fmt, Args &&... args)
	{
		get_logger (type).debug (fmt, std::forward<Args> (args)...);
	}

	template <class... Args>
	void info (nano::log::type type, spdlog::format_string_t<Args...> fmt, Args &&... args)
	{
		get_logger (type).info (fmt, std::forward<Args> (args)...);
	}

	template <class... Args>
	void warn (nano::log::type type, spdlog::format_string_t<Args...> fmt, Args &&... args)
	{
		get_logger (type).warn (fmt, std::forward<Args> (args)...);
	}

	template <class... Args>
	void error (nano::log::type type, spdlog::format_string_t<Args...> fmt, Args &&... args)
	{
		get_logger (type).error (fmt, std::forward<Args> (args)...);
	}

	template <class... Args>
	void critical (nano::log::type type, spdlog::format_string_t<Args...> fmt, Args &&... args)
	{
		get_logger (type).critical (fmt, std::forward<Args> (args)...);
	}

public:
	template <typename... Args>
	void trace (nano::log::type type, nano::log::detail detail, Args &&... args)
	{
		if constexpr (is_tracing_enabled ())
		{
			debug_assert (detail != nano::log::detail::all);

			// Include info about precise time of the event
			auto now = std::chrono::high_resolution_clock::now ();

			// TODO: Improve code indentation config
			auto logger = get_logger (type, detail);
			logger.trace ("{}",
			nano::streamed_args (global_tracing_config,
			nano::log::arg{ "event", to_string (std::make_pair (type, detail)) },
			nano::log::arg{ "time", nano::log::microseconds (now) },
			std::forward<Args> (args)...));
		}
	}

private:
	const std::string identifier;

	std::map<nano::log::logger_id, std::shared_ptr<spdlog::logger>> spd_loggers;
	std::shared_mutex mutex;

private:
	spdlog::logger & get_logger (nano::log::type, nano::log::detail = nano::log::detail::all);
	std::shared_ptr<spdlog::logger> make_logger (nano::log::logger_id);
	nano::log::level find_level (nano::log::logger_id) const;

	static spdlog::level::level_enum to_spdlog_level (nano::log::level);
};

/**
 * Returns a logger instance that can be used before node specific logging is available.
 * Should only be used for logging that happens during startup and initialization, since it won't contain node specific identifier.
 */
nano::logger & default_logger ();
}