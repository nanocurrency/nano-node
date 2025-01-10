#pragma once

#include <celerix/lib/numbers.hpp>
#include <celerix/node/fwd.hpp>

namespace celerix
{
class bucketing
{
public:
	bucketing ();

	celerix::bucket_index bucket_index (celerix::amount balance) const;
	std::vector<celerix::bucket_index> const & bucket_indices () const;
	size_t size () const;

private:
	std::vector<celerix::uint128_t> minimums;
	std::vector<celerix::bucket_index> indices;
};
}