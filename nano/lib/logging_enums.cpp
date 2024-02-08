#include <nano/lib/logging_enums.hpp>
#include <nano/lib/utility.hpp>

#include <magic_enum.hpp>

std::string_view nano::log::to_string (nano::log::type tag)
{
	return magic_enum::enum_name (tag);
}

std::string_view nano::log::to_string (nano::log::detail detail)
{
	return magic_enum::enum_name (detail);
}

std::string_view nano::log::to_string (nano::log::level level)
{
	return magic_enum::enum_name (level);
}

const std::vector<nano::log::level> & nano::log::all_levels ()
{
	static std::vector<nano::log::level> all = [] () {
		return nano::util::enum_values<nano::log::level> ();
	}();
	return all;
}

const std::vector<nano::log::type> & nano::log::all_types ()
{
	static std::vector<nano::log::type> all = [] () {
		return nano::util::enum_values<nano::log::type> ();
	}();
	return all;
}

nano::log::level nano::log::parse_level (std::string_view name)
{
	auto value = nano::util::parse_enum<nano::log::level> (name);
	if (value.has_value ())
	{
		return value.value ();
	}
	else
	{
		auto all_levels_str = nano::util::join (nano::log::all_levels (), ", ", [] (auto const & lvl) {
			return to_string (lvl);
		});

		throw std::invalid_argument ("Invalid log level: " + std::string (name) + ". Must be one of: " + all_levels_str);
	}
}

nano::log::type nano::log::parse_type (std::string_view name)
{
	auto value = nano::util::parse_enum<nano::log::type> (name);
	if (value.has_value ())
	{
		return value.value ();
	}
	else
	{
		throw std::invalid_argument ("Invalid log type: " + std::string (name));
	}
}

nano::log::detail nano::log::parse_detail (std::string_view name)
{
	auto value = nano::util::parse_enum<nano::log::detail> (name);
	if (value.has_value ())
	{
		return value.value ();
	}
	else
	{
		throw std::invalid_argument ("Invalid log detail: " + std::string (name));
	}
}

std::string_view nano::log::to_string (nano::log::tracing_format format)
{
	return magic_enum::enum_name (format);
}

nano::log::tracing_format nano::log::parse_tracing_format (std::string_view name)
{
	auto value = magic_enum::enum_cast<nano::log::tracing_format> (name);
	if (value.has_value ())
	{
		return value.value ();
	}
	else
	{
		auto all_formats_str = nano::util::join (nano::log::all_tracing_formats (), ", ", [] (auto const & fmt) {
			return to_string (fmt);
		});

		throw std::invalid_argument ("Invalid tracing format: " + std::string (name) + ". Must be one of: " + all_formats_str);
	}
}

const std::vector<nano::log::tracing_format> & nano::log::all_tracing_formats ()
{
	static std::vector<nano::log::tracing_format> all = [] () {
		std::vector<nano::log::tracing_format> result;
		for (auto const & fmt : magic_enum::enum_values<nano::log::tracing_format> ())
		{
			result.push_back (fmt);
		}
		return result;
	}();
	return all;
}