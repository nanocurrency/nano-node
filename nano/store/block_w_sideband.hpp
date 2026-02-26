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

// Legacy sideband format: includes successor field (used during migrations)
class block_w_sideband_legacy
{
public:
	std::shared_ptr<nano::block> block;
	nano::block_sideband sideband;
};
}
