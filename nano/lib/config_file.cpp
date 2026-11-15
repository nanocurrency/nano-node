#include <nano/lib/config_file.hpp>
#include <nano/lib/open_file.hpp>

#include <iostream>

// Using std::cerr here, since logging may not be initialized yet
nano::error nano::read_config_file (nano::tomlconfig & toml, std::string_view filename, std::filesystem::path const & data_path, std::vector<std::string> const & overrides)
{
	auto const path = data_path / filename;
	auto file = nano::open_file (path);
	if (file)
	{
		if (auto error = toml.read (file.value ()))
		{
			return prefix_config_error (error, filename);
		}
		std::cerr << "Config file `" << filename << "` loaded from node data directory: " << path.string () << std::endl;
	}
	else if (file.error () == std::make_error_code (std::errc::no_such_file_or_directory))
	{
		// Reading a file replaces the document, so a missing one starts from an empty document too
		toml = nano::tomlconfig{};
		std::cerr << "Config file `" << filename << "` not found, using default configuration" << std::endl;
	}
	else
	{
		return prefix_config_error (file.error (), filename);
	}
	return prefix_config_error (toml.apply_overrides (overrides), filename);
}

nano::error nano::prefix_config_error (nano::error error, std::string_view filename)
{
	if (error)
	{
		error.set_message (std::string{ filename } + ": " + error.get_message ());
	}
	return error;
}
