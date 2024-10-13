#pragma once

#include <nano/lib/block_sideband.hpp>

#include <memory>

namespace nano
{
class block;
}
namespace nano::store
{
class block_w_sideband
{
public:
	std::shared_ptr<nano::block> block;
	nano::block_sideband sideband;
};
}
