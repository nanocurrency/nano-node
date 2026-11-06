#include <nano/lib/enum_util.hpp>
#include <nano/node/transport/common.hpp>

std::string_view nano::transport::to_string (socket_type type)
{
	return nano::enum_to_string (type);
}

std::string_view nano::transport::to_string (socket_endpoint type)
{
	return nano::enum_to_string (type);
}

std::string_view nano::transport::to_string (connection_type type)
{
	return nano::enum_to_string (type);
}
