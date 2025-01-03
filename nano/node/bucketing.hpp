#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/node/fwd.hpp>

namespace nano
{
class bucketing
{
public:
	bucketing ();

	nano::bucket_index bucket_index (nano::amount balance) const;
	std::vector<nano::bucket_index> const & bucket_indices () const;
	size_t size () const;

private:
	std::vector<nano::uint128_t> minimums;
	std::vector<nano::bucket_index> indices;
};
}