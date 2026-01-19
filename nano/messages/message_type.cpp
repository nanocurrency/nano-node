#include <nano/lib/enum_util.hpp>
#include <nano/lib/logging_enums.hpp>
#include <nano/lib/stats_enums.hpp>
#include <nano/messages/message_type.hpp>

namespace nano::messages
{
std::string_view to_string (message_type type)
{
	return nano::enum_to_string (type);
}

nano::stat::detail to_stat_detail (message_type type)
{
	return nano::enum_convert<nano::stat::detail> (type);
}

nano::log::detail to_log_detail (message_type type)
{
	return nano::enum_convert<nano::log::detail> (type);
}
}
