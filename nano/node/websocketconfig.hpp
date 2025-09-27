#pragma once

#include <nano/lib/constants.hpp>
#include <nano/lib/errors.hpp>

#include <memory>

namespace nano
{
class tomlconfig;
namespace websocket
{
	/** websocket configuration */
	class config final
	{
	public:
		config (nano::network_constants const &);

		nano::error deserialize_toml (nano::tomlconfig &);
		nano::error serialize_toml (nano::tomlconfig &) const;

		nano::network_constants const & network_constants;
		bool enabled{ false };
		uint16_t port;
		std::string address;
	};
}
}
