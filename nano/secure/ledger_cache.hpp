#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/secure/rep_weights.hpp>
#include <nano/store/rep_weight.hpp>

#include <atomic>

namespace nano
{
class ledger;
}
namespace nano::store
{
class component;
}

namespace nano
{
/* Holds an in-memory cache of various counts */
class ledger_cache
{
	friend class store::component;
	friend class ledger;

public:
	explicit ledger_cache (nano::store::rep_weight & rep_weight_store_a, nano::uint128_t min_rep_weight_a = 0);
	nano::rep_weights rep_weights;

private:
	std::atomic<uint64_t> cemented_count{ 0 };
	std::atomic<uint64_t> block_count{ 0 };
	std::atomic<uint64_t> pruned_count{ 0 };
	std::atomic<uint64_t> account_count{ 0 };
};
}
