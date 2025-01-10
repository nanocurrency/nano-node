#include <celerix/lib/enum_util.hpp>
#include <celerix/lib/stats_enums.hpp>

std::string_view celerix::to_string (celerix::stat::type type)
{
	return celerix::enum_util::name (type);
}

std::string_view celerix::to_string (celerix::stat::detail detail)
{
	return celerix::enum_util::name (detail);
}

std::string_view celerix::to_string (celerix::stat::dir dir)
{
	return celerix::enum_util::name (dir);
}

std::string_view celerix::to_string (celerix::stat::sample sample)
{
	return celerix::enum_util::name (sample);
}