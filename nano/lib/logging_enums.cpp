#include <celerix/lib/enum_util.hpp>
#include <celerix/lib/logging_enums.hpp>
#include <celerix/lib/utility.hpp>

std::string_view celerix::log::to_string (celerix::log::type tag)
{
	return celerix::enum_util::name (tag);
}

std::string_view celerix::log::to_string (celerix::log::detail detail)
{
	return celerix::enum_util::name (detail);
}

std::string_view celerix::log::to_string (celerix::log::level level)
{
	return celerix::enum_util::name (level);
}

const std::vector<celerix::log::level> & celerix::log::all_levels ()
{
	return celerix::enum_util::values<celerix::log::level> ();
}

const std::vector<celerix::log::type> & celerix::log::all_types ()
{
	return celerix::enum_util::values<celerix::log::type> ();
}

celerix::log::level celerix::log::parse_level (std::string_view name)
{
	auto value = celerix::enum_util::try_parse<celerix::log::level> (name);
	if (value.has_value ())
	{
		return value.value ();
	}
	auto all_levels_str = celerix::util::join (celerix::log::all_levels (), ", ", [] (auto const & lvl) {
		return to_string (lvl);
	});
	throw std::invalid_argument ("Invalid log level: " + std::string (name) + ". Must be one of: " + all_levels_str);
}

celerix::log::type celerix::log::parse_type (std::string_view name)
{
	auto value = celerix::enum_util::try_parse<celerix::log::type> (name);
	if (value.has_value ())
	{
		return value.value ();
	}
	throw std::invalid_argument ("Invalid log type: " + std::string (name));
}

celerix::log::detail celerix::log::parse_detail (std::string_view name)
{
	auto value = celerix::enum_util::try_parse<celerix::log::detail> (name);
	if (value.has_value ())
	{
		return value.value ();
	}
	throw std::invalid_argument ("Invalid log detail: " + std::string (name));
}

std::string_view celerix::log::to_string (celerix::log::tracing_format format)
{
	return celerix::enum_util::name (format);
}

celerix::log::tracing_format celerix::log::parse_tracing_format (std::string_view name)
{
	auto value = celerix::enum_util::try_parse<celerix::log::tracing_format> (name);
	if (value.has_value ())
	{
		return value.value ();
	}
	auto all_formats_str = celerix::util::join (celerix::log::all_tracing_formats (), ", ", [] (auto const & fmt) {
		return to_string (fmt);
	});
	throw std::invalid_argument ("Invalid tracing format: " + std::string (name) + ". Must be one of: " + all_formats_str);
}

const std::vector<celerix::log::tracing_format> & celerix::log::all_tracing_formats ()
{
	return celerix::enum_util::values<celerix::log::tracing_format> ();
}