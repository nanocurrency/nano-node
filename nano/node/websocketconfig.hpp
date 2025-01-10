#pragma once

#include <celerix/lib/constants.hpp>
#include <celerix/lib/errors.hpp>

#include <memory>

namespace celerix
{
class tomlconfig;
namespace websocket
{
	/** websocket configuration */
	class config final
	{
	public:
		config (celerix::network_constants & network_constants);
		celerix::error deserialize_toml (celerix::tomlconfig & toml_a);
		celerix::error serialize_toml (celerix::tomlconfig & toml) const;
		celerix::network_constants & network_constants;
		bool enabled{ false };
		uint16_t port;
		std::string address;
	};
}
}
