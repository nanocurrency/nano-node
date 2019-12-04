#pragma once

#include <nano/lib/config.hpp>
#include <nano/lib/errors.hpp>

namespace nano
{
class jsonconfig;
class tomlconfig;
namespace websocket
{
	/** websocket configuration */
	class config final
	{
	public:
		config ();
		nano::error deserialize_json (nano::jsonconfig & json_a);
		nano::error serialize_json (nano::jsonconfig & json) const;
		nano::error deserialize_toml (nano::tomlconfig & toml_a);
		nano::error serialize_toml (nano::tomlconfig & toml) const;
		nano::network_constants network_constants;
		bool enabled{ false };
		uint16_t port;
		std::string address;
	};
}
}
