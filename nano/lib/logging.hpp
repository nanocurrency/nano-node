#pragma once

#include <nano/lib/logging_enums.hpp>
#include <nano/lib/tomlconfig.hpp>

#include <initializer_list>
#include <memory>
#include <shared_mutex>
#include <sstream>

#include <spdlog/spdlog.h>

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

	using logger_id_t = std::pair<nano::log::type, nano::log::detail>;
	std::map<logger_id_t, nano::log::level> levels;

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

	static logger_id_t parse_logger_id (std::string const &);

private:
	/// Returns placeholder log levels for all loggers
	static std::map<logger_id_t, nano::log::level> default_levels (nano::log::level);
};

/// @throws std::runtime_error if the log config file is malformed
nano::log_config load_log_config (nano::log_config fallback, std::filesystem::path const & data_path = {}, std::vector<std::string> const & config_overrides = std::vector<std::string> ());
}

namespace nano
{
spdlog::level::level_enum to_spdlog_level (nano::log::level);

class logger final
{
public:
	logger (std::string identifier = "");

	// Disallow copies
	logger (logger const &) = delete;

public:
	static void initialize (nano::log_config fallback, std::filesystem::path data_path = {}, std::vector<std::string> const & config_overrides = std::vector<std::string> ());
	static void initialize_for_tests (nano::log_config fallback);
	static void flush ();

private:
	static bool global_initialized;
	static nano::log_config global_config;
	static std::vector<spdlog::sink_ptr> global_sinks;
	static std::function<std::string (nano::log::type tag, std::string identifier)> global_name_formatter;

	static void initialize_common (nano::log_config const &, std::filesystem::path data_path);

public:
	template <class... Args>
	void log (nano::log::level level, nano::log::type tag, spdlog::format_string_t<Args...> fmt, Args &&... args)
	{
		get_logger (tag).log (to_spdlog_level (level), fmt, std::forward<Args> (args)...);
	}

	template <class... Args>
	void debug (nano::log::type tag, spdlog::format_string_t<Args...> fmt, Args &&... args)
	{
		get_logger (tag).debug (fmt, std::forward<Args> (args)...);
	}

	template <class... Args>
	void info (nano::log::type tag, spdlog::format_string_t<Args...> fmt, Args &&... args)
	{
		get_logger (tag).info (fmt, std::forward<Args> (args)...);
	}

	template <class... Args>
	void warn (nano::log::type tag, spdlog::format_string_t<Args...> fmt, Args &&... args)
	{
		get_logger (tag).warn (fmt, std::forward<Args> (args)...);
	}

	template <class... Args>
	void error (nano::log::type tag, spdlog::format_string_t<Args...> fmt, Args &&... args)
	{
		get_logger (tag).error (fmt, std::forward<Args> (args)...);
	}

	template <class... Args>
	void critical (nano::log::type tag, spdlog::format_string_t<Args...> fmt, Args &&... args)
	{
		get_logger (tag).critical (fmt, std::forward<Args> (args)...);
	}

private:
	const std::string identifier;

	std::unordered_map<nano::log::type, std::shared_ptr<spdlog::logger>> spd_loggers;
	std::shared_mutex mutex;

private:
	spdlog::logger & get_logger (nano::log::type tag);
	std::shared_ptr<spdlog::logger> make_logger (nano::log::type tag);
};

nano::logger & default_logger ();
}