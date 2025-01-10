#include <celerix/lib/blocks.hpp>
#include <celerix/lib/utility.hpp>
#include <celerix/node/bucketing.hpp>
#include <celerix/secure/ledger.hpp>
#include <celerix/secure/ledger_set_any.hpp>

celerix::bucketing::bucketing ()
{
	auto build_region = [this] (uint128_t const & begin, uint128_t const & end, size_t count) {
		auto width = (end - begin) / count;
		for (auto i = 0; i < count; ++i)
		{
			minimums.push_back (begin + i * width);
		}
	};

	minimums.push_back (uint128_t{ 0 });
	build_region (uint128_t{ 1 } << 79, uint128_t{ 1 } << 88, 1);
	build_region (uint128_t{ 1 } << 88, uint128_t{ 1 } << 92, 2);
	build_region (uint128_t{ 1 } << 92, uint128_t{ 1 } << 96, 4);
	build_region (uint128_t{ 1 } << 96, uint128_t{ 1 } << 100, 8);
	build_region (uint128_t{ 1 } << 100, uint128_t{ 1 } << 104, 16);
	build_region (uint128_t{ 1 } << 104, uint128_t{ 1 } << 108, 16);
	build_region (uint128_t{ 1 } << 108, uint128_t{ 1 } << 112, 8);
	build_region (uint128_t{ 1 } << 112, uint128_t{ 1 } << 116, 4);
	build_region (uint128_t{ 1 } << 116, uint128_t{ 1 } << 120, 2);
	minimums.push_back (uint128_t{ 1 } << 120);

	for (auto i = 0; i < minimums.size (); ++i)
	{
		indices.push_back (i);
	}
}

celerix::bucket_index celerix::bucketing::bucket_index (celerix::amount balance) const
{
	release_assert (!minimums.empty ());
	auto it = std::upper_bound (minimums.begin (), minimums.end (), balance);
	release_assert (it != minimums.begin ()); // There should always be a bucket with a minimum_balance of 0
	return std::distance (minimums.begin (), std::prev (it));
}

std::vector<celerix::bucket_index> const & celerix::bucketing::bucket_indices () const
{
	return indices;
}

size_t celerix::bucketing::size () const
{
	return minimums.size ();
}