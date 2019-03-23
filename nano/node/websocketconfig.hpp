#pragma once

#include <boost/asio.hpp>
#include <nano/lib/errors.hpp>
#include <string>

namespace nano
{
class jsonconfig;
namespace websocket
{
	/** websocket configuration */
	class config
	{
	public:
		nano::error deserialize_json (nano::jsonconfig & json_a);
		nano::error serialize_json (nano::jsonconfig & json) const;
		bool enabled{ false };
		uint16_t port{ 7078 };
		boost::asio::ip::address_v6 address{ boost::asio::ip::address_v6::loopback () };
	};
}
}
