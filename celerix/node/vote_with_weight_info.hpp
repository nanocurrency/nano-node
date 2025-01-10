#pragma once

#include <celerix/lib/numbers.hpp>

#include <chrono>

namespace celerix
{
class vote_with_weight_info final
{
public:
	celerix::account representative;
	std::chrono::steady_clock::time_point time;
	uint64_t timestamp;
	celerix::block_hash hash;
	celerix::uint128_t weight;
};
}
