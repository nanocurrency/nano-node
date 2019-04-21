#pragma once

#include <nano/lib/errors.hpp>
#include <nano/lib/jsonconfig.hpp>

namespace nano
{
class opencl_config
{
public:
	opencl_config () = default;
	opencl_config (unsigned, unsigned, unsigned);
	nano::error serialize_json (nano::jsonconfig &) const;
	nano::error deserialize_json (nano::jsonconfig &);
	unsigned platform{ 0 };
	unsigned device{ 0 };
	unsigned threads{ 1024 * 1024 };
};
}
