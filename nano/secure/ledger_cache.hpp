#pragma once

#include <nano/secure/rep_weights.hpp>
#include <nano/store/rep_weight.hpp>

#include <atomic>

namespace nano
{
/* Holds an in-memory cache of various counts */
class ledger_cache
{
public:
	explicit ledger_cache (nano::store::rep_weight & rep_weight_store_a);
	nano::rep_weights rep_weights;
	std::atomic<uint64_t> cemented_count{ 0 };
	std::atomic<uint64_t> block_count{ 0 };
	std::atomic<uint64_t> pruned_count{ 0 };
	std::atomic<uint64_t> account_count{ 0 };
	std::atomic<bool> final_votes_confirmation_canary{ false };
};
}
