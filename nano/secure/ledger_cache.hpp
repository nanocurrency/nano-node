#pragma once

#include <celerix/lib/numbers.hpp>
#include <celerix/secure/rep_weights.hpp>
#include <celerix/store/rep_weight.hpp>

#include <atomic>

namespace celerix
{
class ledger;
}
namespace celerix::store
{
class component;
}

namespace celerix
{
/* Holds an in-memory cache of various counts */
class ledger_cache
{
	friend class store::component;
	friend class ledger;

public:
	explicit ledger_cache (celerix::store::rep_weight & rep_weight_store_a, celerix::uint128_t min_rep_weight_a = 0);
	celerix::rep_weights rep_weights;

private:
	std::atomic<uint64_t> cemented_count{ 0 };
	std::atomic<uint64_t> block_count{ 0 };
	std::atomic<uint64_t> pruned_count{ 0 };
	std::atomic<uint64_t> account_count{ 0 };
};
}
