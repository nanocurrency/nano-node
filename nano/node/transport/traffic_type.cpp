#include <nano/lib/enum_util.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/transport/traffic_type.hpp>

std::string_view nano::transport::to_string (nano::transport::traffic_type type)
{
	return nano::enum_to_string (type);
}

nano::stat::detail nano::transport::to_stat_detail (nano::transport::traffic_type type)
{
	return nano::enum_convert<nano::stat::detail> (type);
}

std::vector<nano::transport::traffic_type> const & nano::transport::all_traffic_types ()
{
	return nano::enum_values<nano::transport::traffic_type> ();
}

size_t nano::transport::traffic_priority (nano::transport::traffic_type type)
{
	switch (type)
	{
		case nano::transport::traffic_type::block_broadcast:
		case nano::transport::traffic_type::vote_rebroadcast:
			return 1;
		default:
			return 4;
	}
}