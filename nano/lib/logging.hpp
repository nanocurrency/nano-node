#pragma once

#include <nano/lib/logging_enums.hpp>
#include <nano/lib/tomlconfig.hpp>

#include <initializer_list>
#include <memory>
#include <shared_mutex>
#include <sstream>

#include <spdlog/spdlog.h>

namespace nano::log
{
using logger_id = std::pair<nano::log::type, nano::log::detail>;

std::string to_string (logger_id);
logger_id parse_logger_id (std::string const &);
}

namespace nano
{
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
	static void initialize (nano::log_config fallback, std::optional<std::filesystem::path> data_path = std::nullopt, std::vector<std::string> const & config_overrides = std::vector<std::string> ());
	static void initialize_for_tests (nano::log_config fallback);
	static void flush ();

private:
	static bool global_initialized;
	static nano::log_config global_config;
	static std::vector<spdlog::sink_ptr> global_sinks;
	static std::function<std::string (nano::log::logger_id, std::string identifier)> global_name_formatter;

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