#include <celerix/lib/enum_util.hpp>
#include <celerix/lib/utility.hpp>
#include <celerix/node/transport/traffic_type.hpp>

#include <magic_enum.hpp>

std::string_view celerix::transport::to_string (celerix::transport::traffic_type type)
{
	return celerix::enum_util::name (type);
}

std::vector<celerix::transport::traffic_type> celerix::transport::all_traffic_types ()
{
	return celerix::enum_util::values<celerix::transport::traffic_type> ();
}

celerix::stat::detail celerix::transport::to_stat_detail (celerix::transport::traffic_type type)
{
	return celerix::enum_util::cast<celerix::stat::detail> (type);
}