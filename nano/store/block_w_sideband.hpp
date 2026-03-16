#pragma once

#include <nano/lib/block_sideband.hpp>

#include <memory>

namespace nano::store
{
class block_w_sideband
{
public:
	std::shared_ptr<nano::block> block;
	nano::block_sideband sideband;
};

// Snapshot of block + sideband at v25 format which needs to be read for the v25 to v26 upgrade
class block_w_sideband_v25
{
public:
	std::shared_ptr<nano::block> block;
	nano::block_sideband_v25 sideband;
};
}
