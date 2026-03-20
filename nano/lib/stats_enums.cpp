#include <nano/lib/enum_util.hpp>
#include <nano/lib/stats_enums.hpp>

std::string_view nano::stat::to_string (nano::stat::type type)
{
	return nano::enum_to_string (type);
}

std::string_view nano::stat::to_string (nano::stat::detail detail)
{
	return nano::enum_to_string (detail);
}

std::string_view nano::stat::to_string (nano::stat::dir dir)
{
	return nano::enum_to_string (dir);
}

std::string_view nano::stat::to_string (nano::stat::sample sample)
{
	return nano::enum_to_string (sample);
}
