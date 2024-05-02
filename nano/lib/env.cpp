#include <nano/lib/env.hpp>

#include <boost/algorithm/string.hpp>

#include <string>

std::optional<std::string> nano::get_env (std::string_view name)
{
	std::string name_str{ name };
	if (auto value = std::getenv (name_str.c_str ()))
	{
		return std::string{ value };
	}
	return std::nullopt;
}

std::optional<bool> nano::get_env_bool (std::string_view name)
{
	std::vector<std::string> const on_values{ "1", "true", "on" };
	std::vector<std::string> const off_values{ "0", "false", "off" };

	if (auto value = get_env (name))
	{
		// Using case-insensitive comparison
		if (std::any_of (on_values.begin (), on_values.end (), [&value] (auto const & on) { return boost::iequals (*value, on); }))
		{
			return true;
		}
		if (std::any_of (off_values.begin (), off_values.end (), [&value] (auto const & off) { return boost::iequals (*value, off); }))
		{
			return false;
		}

		throw std::invalid_argument ("Invalid environment boolean value: " + *value);
	}
	return std::nullopt;
}