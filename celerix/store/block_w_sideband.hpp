#pragma once

#include <celerix/lib/block_sideband.hpp>

#include <memory>

namespace celerix
{
class block;
}
namespace celerix::store
{
class block_w_sideband
{
public:
	std::shared_ptr<celerix::block> block;
	celerix::block_sideband sideband;
};
}
