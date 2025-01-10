#pragma once

#include <celerix/lib/errors.hpp>

namespace celerix
{
class tomlconfig;
class opencl_config
{
public:
	opencl_config () = default;
	opencl_config (unsigned, unsigned, unsigned);
	celerix::error serialize_toml (celerix::tomlconfig &) const;
	celerix::error deserialize_toml (celerix::tomlconfig &);
	unsigned platform{ 0 };
	unsigned device{ 0 };
	unsigned threads{ 1024 * 1024 };
};
}
