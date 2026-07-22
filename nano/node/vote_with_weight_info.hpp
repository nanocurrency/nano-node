#pragma once

#include <nano/lib/numbers.hpp>

#include <chrono>

namespace nano
{
class vote_with_weight_info final
{
public:
	nano::account representative;
	std::chrono::steady_clock::time_point time;
	uint64_t timestamp;
	nano::block_hash hash;
	nano::uint128_t weight;
};
}
