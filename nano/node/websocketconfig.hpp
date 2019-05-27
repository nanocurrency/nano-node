#pragma once

#include <nano/lib/config.hpp>
#include <nano/lib/errors.hpp>

#include <boost/asio.hpp>

namespace nano
{
class jsonconfig;
namespace websocket
{
	/** websocket configuration */
	class config final
	{
	public:
		config ();
		nano::error deserialize_json (nano::jsonconfig & json_a);
		nano::error serialize_json (nano::jsonconfig & json) const;
		nano::network_constants network_constants;
		bool enabled{ false };
		uint16_t port;
		boost::asio::ip::address_v6 address{ boost::asio::ip::address_v6::loopback () };
	};
}
}
