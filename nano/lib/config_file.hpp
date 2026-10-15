#pragma once

#include <nano/lib/errors.hpp>
#include <nano/lib/tomlconfig.hpp>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace nano
{
/**
 * Reads the configuration file `filename` from `data_path` into `toml` and applies `overrides` on top of it.
 * Overrides are `key=value` entries and take precedence over the file.
 * A missing file is not an error and is never created; the result then holds only the overrides.
 * Reports on stderr whether the file was found.
 * A returned error names `filename` in its message.
 */
nano::error read_config_file (nano::tomlconfig & toml, std::string_view filename, std::filesystem::path const & data_path, std::vector<std::string> const & overrides = {});

/** Prefixes the message of a failed \p error with \p filename, so that the report names the file it is about */
nano::error prefix_config_error (nano::error error, std::string_view filename);

/** Reads the configuration file as above and deserializes it into `config`, which keeps its current values for every key the file does not mention */
template <typename T>
nano::error read_config_file (T & config, std::string_view filename, std::filesystem::path const & data_path, std::vector<std::string> const & overrides = {})
{
	nano::tomlconfig toml;
	if (auto error = read_config_file (toml, filename, data_path, overrides))
	{
		return error;
	}
	return prefix_config_error (config.deserialize_toml (toml), filename);
}

/**
 * Reads the configuration file on top of `config` as `read_config_file` and returns the result.
 * @throws std::runtime_error if the file or overrides are not valid toml or deserialization fails
 */
template <typename T>
T load_config_file (T config, std::string_view filename, std::filesystem::path const & data_path, std::vector<std::string> const & overrides = {})
{
	if (auto error = read_config_file (config, filename, data_path, overrides))
	{
		throw std::runtime_error (error.get_message ());
	}
	return config;
}

/** As above, on top of a default-constructed `T` */
template <typename T>
T load_config_file (std::string_view filename, std::filesystem::path const & data_path, std::vector<std::string> const & overrides = {})
{
	return load_config_file (T{}, filename, data_path, overrides);
}
}
