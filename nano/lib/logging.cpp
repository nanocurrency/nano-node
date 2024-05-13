#include <nano/lib/config.hpp>
#include <nano/lib/enum_util.hpp>
#include <nano/lib/env.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/logging_enums.hpp>
#include <nano/lib/utility.hpp>

#include <fmt/chrono.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/stdout_sinks.h>

nano::logger & nano::default_logger ()
{
	static nano::logger logger{ "default" };
	return logger;
}

/*
 * logger
 */

bool nano::logger::global_initialized{ false };
nano::log_config nano::logger::global_config{};
std::vector<spdlog::sink_ptr> nano::logger::global_sinks{};
nano::object_stream_config nano::logger::global_tracing_config{};

// By default, use only the tag as the logger name, since only one node is running in the process
std::function<std::string (nano::log::logger_id, std::string identifier)> nano::logger::global_name_formatter{ [] (nano::log::logger_id logger_id, std::string identifier) {
	return to_string (logger_id);
} };

void nano::logger::initialize (nano::log_config fallback, std::optional<std::filesystem::path> data_path, std::vector<std::string> const & config_overrides)
{
	// Only load log config from file if data_path is available (i.e. not running in cli mode)
	nano::log_config config = data_path ? nano::load_log_config (fallback, *data_path, config_overrides) : fallback;
	initialize_common (config, data_path);
	global_initialized = true;
}

// Custom log formatter flags
namespace
{
/// Takes a qualified identifier in the form `node_identifier::tag` and splits it into a pair of `identifier` and `tag`
/// It is a limitation of spldlog that we cannot attach additional data to the logger, so we have to encode the node identifier in the logger name
/// @returns <node identifier, tag>
std::pair<std::string_view, std::string_view> split_qualified_identifier (std::string_view qualified_identifier)
{
	auto pos = qualified_identifier.find ("::");
	debug_assert (pos != std::string_view::npos); // This should never happen, since the default logger name formatter always adds the tag
	if (pos == std::string_view::npos)
	{
		return { std::string_view{}, qualified_identifier };
	}
	else
	{
		return { qualified_identifier.substr (0, pos), qualified_identifier.substr (pos + 2) };
	}
}

class identifier_formatter_flag : public spdlog::custom_flag_formatter
{
public:
	void format (const spdlog::details::log_msg & msg, const std::tm & tm, spdlog::memory_buf_t & dest) override
	{
		// Extract identifier and tag from logger name
		auto [identifier, tag] = split_qualified_identifier (std::string_view (msg.logger_name.data (), msg.logger_name.size ()));
		dest.append (identifier.data (), identifier.data () + identifier.size ());
	}

	std::unique_ptr<custom_flag_formatter> clone () const override
	{
		return spdlog::details::make_unique<identifier_formatter_flag> ();
	}
};

class tag_formatter_flag : public spdlog::custom_flag_formatter
{
public:
	void format (const spdlog::details::log_msg & msg, const std::tm & tm, spdlog::memory_buf_t & dest) override
	{
		// Extract identifier and tag from logger name
		auto [identifier, tag] = split_qualified_identifier (std::string_view (msg.logger_name.data (), msg.logger_name.size ()));
		dest.append (tag.data (), tag.data () + tag.size ());
	}

	std::unique_ptr<custom_flag_formatter> clone () const override
	{
		return spdlog::details::make_unique<tag_formatter_flag> ();
	}
};
}

void nano::logger::initialize_for_tests (nano::log_config fallback)
{
	auto config = nano::load_log_config (std::move (fallback), /* load log config from current workdir */ std::filesystem::current_path ());
	initialize_common (config, /* store log file in current workdir */ std::filesystem::current_path ());

	// Use tag and identifier as the logger name, since multiple nodes may be running in the same process
	global_name_formatter = [] (nano::log::logger_id logger_id, std::string identifier) {
		return fmt::format ("{}::{}", identifier, to_string (logger_id));
	};

	// Setup formatter to include information about node identifier `[%i]` and tag `[%n]`
	auto formatter = std::make_unique<spdlog::pattern_formatter> ();
	formatter->add_flag<identifier_formatter_flag> ('i');
	formatter->add_flag<tag_formatter_flag> ('n');
	formatter->set_pattern ("[%Y-%m-%d %H:%M:%S.%e] [%i] [%n] [%l] %v");

	for (auto & sink : global_sinks)
	{
		sink->set_formatter (formatter->clone ());
	}

	global_initialized = true;
}

// Using std::cerr here, since logging may not be initialized yet
void nano::logger::initialize_common (nano::log_config const & config, std::optional<std::filesystem::path> data_path)
{
	global_config = config;

	spdlog::set_automatic_registration (false);
	spdlog::set_level (to_spdlog_level (config.default_level));

	global_sinks.clear ();

	// Console setup
	if (config.console.enable)
	{
		if (!config.console.to_cerr)
		{
			// Only use colors if not writing to cerr
			if (config.console.colors)
			{
				auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt> ();
				global_sinks.push_back (console_sink);
			}
			else
			{
				auto console_sink = std::make_shared<spdlog::sinks::stdout_sink_mt> ();
				global_sinks.push_back (console_sink);
			}
		}
		else
		{
			if (config.console.colors)
			{
				std::cerr << "WARNING: Logging to cerr is enabled, console colors will be disabled" << std::endl;
			}

			auto cerr_sink = std::make_shared<spdlog::sinks::stderr_sink_mt> ();
			global_sinks.push_back (cerr_sink);
		}
	}

	// File setup
	if (config.file.enable)
	{
		// In cases where data_path is not available, file logging should always be disabled
		release_assert (data_path);

		auto now = std::chrono::system_clock::now ();
		auto time = std::chrono::system_clock::to_time_t (now);

		auto filename = fmt::format ("log_{:%Y-%m-%d_%H-%M}-{:%S}", fmt::localtime (time), now.time_since_epoch ());
		std::replace (filename.begin (), filename.end (), '.', '_'); // Replace millisecond dot separator with underscore

		std::filesystem::path log_path{ data_path.value () / "log" / (filename + ".log") };
		log_path = std::filesystem::absolute (log_path);

		std::cerr << "Logging to file: " << log_path.string () << std::endl;

		// If either max_size or rotation_count is 0, then disable file rotation
		if (config.file.max_size == 0 || config.file.rotation_count == 0)
		{
			std::cerr << "WARNING: Log file rotation is disabled, log file size may grow without bound" << std::endl;

			auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt> (log_path.string (), true);
			global_sinks.push_back (file_sink);
		}
		else
		{
			auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt> (log_path.string (), config.file.max_size, config.file.rotation_count);
			global_sinks.push_back (file_sink);
		}
	}

	// Tracing setup
	switch (config.tracing_format)
	{
		case nano::log::tracing_format::standard:
			global_tracing_config = nano::object_stream_config::default_config ();
			break;
		case nano::log::tracing_format::json:
			global_tracing_config = nano::object_stream_config::json_config ();
			break;
	}
}

void nano::logger::flush ()
{
	for (auto & sink : global_sinks)
	{
		sink->flush ();
	}
}

/*
 * logger
 */

nano::logger::logger (std::string identifier) :
	identifier{ std::move (identifier) }
{
	release_assert (global_initialized, "logging should be initialized before creating a logger");
}

nano::logger::~logger ()
{
	flush ();
}

spdlog::logger & nano::logger::get_logger (nano::log::type type, nano::log::detail detail)
{
	// This is a two-step process to avoid exclusively locking the mutex in the common case
	{
		std::shared_lock lock{ mutex };

		if (auto it = spd_loggers.find ({ type, detail }); it != spd_loggers.end ())
		{
			return *it->second;
		}
	}
	// Not found, create a new logger
	{
		std::unique_lock lock{ mutex };

		auto [it, inserted] = spd_loggers.emplace (std::make_pair (type, detail), make_logger ({ type, detail }));
		return *it->second;
	}
}

std::shared_ptr<spdlog::logger> nano::logger::make_logger (nano::log::logger_id logger_id)
{
	auto const & config = global_config;
	auto const & sinks = global_sinks;

	auto name = global_name_formatter (logger_id, identifier);
	auto spd_logger = std::make_shared<spdlog::logger> (name, sinks.begin (), sinks.end ());

	spd_logger->set_level (to_spdlog_level (find_level (logger_id)));
	spd_logger->flush_on (to_spdlog_level (config.flush_level));

	return spd_logger;
}

nano::log::level nano::logger::find_level (nano::log::logger_id logger_id) const
{
	auto const & config = global_config;
	auto const & [type, detail] = logger_id;

	// Check for a specific level for this logger
	if (auto it = config.levels.find (logger_id); it != config.levels.end ())
	{
		return it->second;
	}
	// Check for a default level for this logger type
	if (auto it = config.levels.find ({ type, nano::log::detail::all }); it != config.levels.end ())
	{
		return it->second;
	}
	// Use the default level
	return config.default_level;
}

spdlog::level::level_enum nano::logger::to_spdlog_level (nano::log::level level)
{
	switch (level)
	{
		case nano::log::level::off:
			return spdlog::level::off;
		case nano::log::level::critical:
			return spdlog::level::critical;
		case nano::log::level::error:
			return spdlog::level::err;
		case nano::log::level::warn:
			return spdlog::level::warn;
		case nano::log::level::info:
			return spdlog::level::info;
		case nano::log::level::debug:
			return spdlog::level::debug;
		case nano::log::level::trace:
			return spdlog::level::trace;
	}
	debug_assert (false, "Invalid log level");
	return spdlog::level::off;
}

/*
 * logging config presets
 */

nano::log_config nano::log_config::cli_default ()
{
	log_config config{};
	config.default_level = nano::log::level::critical;
	config.console.colors = false; // to avoid printing warning about cerr and colors
	config.console.to_cerr = true; // Use cerr to avoid interference with CLI output that goes to stdout
	config.file.enable = false;
	return config;
}

nano::log_config nano::log_config::daemon_default ()
{
	log_config config{};
	config.default_level = nano::log::level::info;
	return config;
}

nano::log_config nano::log_config::tests_default ()
{
	log_config config{};
	config.default_level = nano::log::level::off;
	config.file.enable = false;
	return config;
}

nano::log_config nano::log_config::sample_config ()
{
	log_config config{};
	config.default_level = nano::log::level::info;
	config.levels = default_levels (nano::log::level::info); // Populate with default levels
	return config;
}

/*
 * logging config
 */

nano::error nano::log_config::serialize_toml (nano::tomlconfig & toml) const
{
	nano::tomlconfig config_toml;
	serialize (config_toml);
	toml.put_child ("log", config_toml);

	return toml.get_error ();
}

nano::error nano::log_config::deserialize_toml (nano::tomlconfig & toml)
{
	try
	{
		auto logging_l = toml.get_optional_child ("log");
		if (logging_l)
		{
			deserialize (*logging_l);
		}
	}
	catch (std::invalid_argument const & ex)
	{
		toml.get_error ().set (ex.what ());
	}

	return toml.get_error ();
}

void nano::log_config::serialize (nano::tomlconfig & toml) const
{
	toml.put ("default_level", std::string{ to_string (default_level) });

	nano::tomlconfig console_config;
	console_config.put ("enable", console.enable);
	console_config.put ("to_cerr", console.to_cerr);
	console_config.put ("colors", console.colors);
	toml.put_child ("console", console_config);

	nano::tomlconfig file_config;
	file_config.put ("enable", file.enable);
	file_config.put ("max_size", file.max_size);
	file_config.put ("rotation_count", file.rotation_count);
	toml.put_child ("file", file_config);

	nano::tomlconfig levels_config;
	for (auto const & [logger_id, level] : levels)
	{
		auto logger_name = to_string (logger_id.first);
		levels_config.put (std::string{ logger_name }, std::string{ to_string (level) });
	}
	toml.put_child ("levels", levels_config);
}

void nano::log_config::deserialize (nano::tomlconfig & toml)
{
	if (toml.has_key ("default_level"))
	{
		auto default_level_l = toml.get<std::string> ("default_level");
		default_level = nano::log::parse_level (default_level_l);
	}

	if (toml.has_key ("console"))
	{
		auto console_config = toml.get_required_child ("console");
		console_config.get ("enable", console.enable);
		console_config.get ("to_cerr", console.to_cerr);
		console_config.get ("colors", console.colors);
	}

	if (toml.has_key ("file"))
	{
		auto file_config = toml.get_required_child ("file");
		file_config.get ("enable", file.enable);
		file_config.get ("max_size", file.max_size);
		file_config.get ("rotation_count", file.rotation_count);
	}

	if (toml.has_key ("levels"))
	{
		auto levels_config = toml.get_required_child ("levels");
		for (auto & level : levels_config.get_values<std::string> ())
		{
			try
			{
				auto & [name_str, level_str] = level;
				auto logger_level = nano::log::parse_level (level_str);
				auto logger_id = nano::log::parse_logger_id (name_str);

				levels[logger_id] = logger_level;
			}
			catch (std::invalid_argument const & ex)
			{
				// Ignore but warn about invalid logger names
				std::cerr << "Problem processing log config: " << ex.what () << std::endl;
			}
		}
	}
}

std::map<nano::log::logger_id, nano::log::level> nano::log_config::default_levels (nano::log::level default_level)
{
	std::map<nano::log::logger_id, nano::log::level> result;
	for (auto const & type : nano::log::all_types ())
	{
		result.emplace (std::make_pair (type, nano::log::detail::all), default_level);
	}
	return result;
}

/*
 * config loading
 */

// Using std::cerr here, since logging may not be initialized yet
nano::log_config nano::load_log_config (nano::log_config fallback, const std::filesystem::path & data_path, const std::vector<std::string> & config_overrides)
{
	const std::string config_filename = "config-log.toml";
	try
	{
		auto config = nano::load_config_file<nano::log_config> (fallback, config_filename, data_path, config_overrides);

		// Parse default log level from environment variable, e.g. "NANO_LOG=debug"
		auto env_level = nano::env::get ("NANO_LOG");
		if (env_level)
		{
			try
			{
				auto level = nano::log::parse_level (*env_level);
				config.default_level = level;

				std::cerr << "Using default log level from NANO_LOG environment variable: " << to_string (level) << std::endl;
			}
			catch (std::invalid_argument const & ex)
			{
				std::cerr << "Invalid log level from NANO_LOG environment variable: " << ex.what () << std::endl;
			}
		}

		// Parse per logger levels from environment variable, e.g. "NANO_LOG_LEVELS=ledger=debug,node=trace"
		if (auto env_levels = nano::env::get ("NANO_LOG_LEVELS"))
		{
			std::map<nano::log::logger_id, nano::log::level> levels;
			for (auto const & env_level_str : nano::util::split (*env_levels, ","))
			{
				try
				{
					// Split 'logger_name=level' into a pair of 'logger_name' and 'level'
					auto arr = nano::util::split (env_level_str, "=");
					if (arr.size () != 2)
					{
						throw std::invalid_argument ("Invalid entry: " + env_level_str);
					}

					auto name_str = arr[0];
					auto level_str = arr[1];

					auto logger_id = nano::log::parse_logger_id (name_str);
					auto logger_level = nano::log::parse_level (level_str);

					levels[logger_id] = logger_level;

					std::cerr << "Using logger log level from NANO_LOG_LEVELS environment variable: " << to_string (logger_id) << "=" << to_string (logger_level) << std::endl;
				}
				catch (std::invalid_argument const & ex)
				{
					std::cerr << "Invalid log level from NANO_LOG_LEVELS environment variable: " << ex.what () << std::endl;
				}
			}

			// Merge with existing levels
			for (auto const & [logger_id, level] : levels)
			{
				config.levels[logger_id] = level;
			}
		}

		if (auto env_tracing_format = nano::env::get ("NANO_TRACE_FORMAT"))
		{
			try
			{
				auto tracing_format = nano::log::parse_tracing_format (*env_tracing_format);
				config.tracing_format = tracing_format;

				std::cerr << "Using trace format from NANO_TRACE_FORMAT environment variable: " << to_string (tracing_format) << std::endl;
			}
			catch (std::invalid_argument const & ex)
			{
				std::cerr << "Invalid trace format from NANO_TRACE_FORMAT environment variable: " << ex.what () << std::endl;
			}
		}

		auto tracing_configured = [&] () {
			if (config.default_level == nano::log::level::trace)
			{
				return true;
			}
			for (auto const & [logger_id, level] : config.levels)
			{
				if (level == nano::log::level::trace)
				{
					return true;
				}
			}
			return false;
		};

		if (tracing_configured () && !is_tracing_enabled ())
		{
			std::cerr << "WARNING: Tracing is not enabled in this build, but log level is set to trace" << std::endl;
		}

		return config;
	}
	catch (std::runtime_error const & ex)
	{
		std::cerr << "Unable to load log config. Using defaults. Error: " << ex.what () << std::endl;
	}
	return fallback;
}

std::string nano::log::to_string (nano::log::logger_id logger_id)
{
	auto const & [type, detail] = logger_id;
	if (detail == nano::log::detail::all)
	{
		return fmt::format ("{}", to_string (type));
	}
	else
	{
		return fmt::format ("{}::{}", to_string (type), to_string (detail));
	}
}

/**
 * Parse `logger_name[:logger_detail]` into a pair of `log::type` and `log::detail`
 * @throw std::invalid_argument if `logger_name` or `logger_detail` are invalid
 */
nano::log::logger_id nano::log::parse_logger_id (const std::string & logger_name)
{
	auto parts = nano::util::split (logger_name, "::");
	if (parts.size () == 1)
	{
		return { nano::log::parse_type (parts[0]), nano::log::detail::all };
	}
	if (parts.size () == 2)
	{
		return { nano::log::parse_type (parts[0]), nano::log::parse_detail (parts[1]) };
	}
	throw std::invalid_argument ("Invalid logger name: " + logger_name);
}