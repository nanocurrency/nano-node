#pragma once

#include <nano/lib/errors.hpp>
#include <nano/lib/timer.hpp>
#include <nano/node/bootstrap/bootstrap_server.hpp>

namespace nano
{
class tomlconfig;

class bootstrap_ascending_config final
{
public:
	nano::error deserialize (nano::tomlconfig & toml);
	nano::error serialize (nano::tomlconfig & toml) const;

	std::size_t requests_limit{ 128 };
	std::size_t database_requests_limit{ 1024 };
	std::size_t pull_count{ nano::bootstrap_server::max_blocks };
	nano::millis_t timeout{ 1000 * 3 };
};
}
