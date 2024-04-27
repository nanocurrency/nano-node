#include <nano/lib/enum_utils.hpp>
#include <nano/lib/stats_enums.hpp>

std::string_view nano::to_string (nano::stat::type type)
{
	return nano::enum_name (type);
}

std::string_view nano::to_string (nano::stat::detail detail)
{
	return nano::enum_name (detail);
}

std::string_view nano::to_string (nano::stat::dir dir)
{
	return nano::enum_name (dir);
}

std::string_view nano::to_string (nano::stat::sample sample)
{
	return nano::enum_name (sample);
}