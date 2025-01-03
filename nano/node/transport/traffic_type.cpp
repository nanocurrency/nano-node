#include <nano/lib/enum_util.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/transport/traffic_type.hpp>

#include <magic_enum.hpp>

std::string_view nano::transport::to_string (nano::transport::traffic_type type)
{
	return nano::enum_util::name (type);
}

std::vector<nano::transport::traffic_type> nano::transport::all_traffic_types ()
{
	return nano::enum_util::values<nano::transport::traffic_type> ();
}

nano::stat::detail nano::transport::to_stat_detail (nano::transport::traffic_type type)
{
	return nano::enum_util::cast<nano::stat::detail> (type);
}