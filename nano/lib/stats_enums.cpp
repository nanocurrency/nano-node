#include <nano/lib/stats_enums.hpp>

#include <magic_enum.hpp>

std::string_view nano::to_string (nano::stat::type type)
{
	return magic_enum::enum_name (type);
}

std::string_view nano::to_string (nano::stat::detail detail)
{
	return magic_enum::enum_name (detail);
}

std::string_view nano::to_string (nano::stat::dir dir)
{
	return magic_enum::enum_name (dir);
}