#pragma once
#include <nano/lib/numbers.hpp>
#include <nano/lib/utility.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>

namespace nano
{
class block;
}
namespace nano::scheduler
{
class bucket;
/** A container for holding blocks and their arrival/creation time.
 *
 *  The container consists of a number of buckets. Each bucket holds an ordered set of 'value_type' items.
 *  The buckets are accessed in a round robin fashion. The index 'current' holds the index of the bucket to access next.
 *  When a block is inserted, the bucket to go into is determined by the account balance and the priority inside that
 *  bucket is determined by its creation/arrival time.
 *
 *  The arrival/creation time is only an approximation and it could even be wildly wrong,
 *  for example, in the event of bootstrapped blocks.
 */
class buckets final
{
	/** container for the buckets to be read in round robin fashion */
	std::map<nano::amount, std::unique_ptr<nano::scheduler::bucket>> buckets_m;

	void setup_buckets (uint64_t maximum);

public:
	buckets (uint64_t maximum = 128);
	~buckets ();

	std::size_t size () const;
	std::size_t bucket_count () const;
	std::size_t bucket_size (nano::amount const & amount) const;
	std::size_t active () const;
	scheduler::bucket * next ();
	bool empty () const;
	void dump () const;
	scheduler::bucket & bucket (nano::uint128_t const & balance) const;

	std::unique_ptr<nano::container_info_component> collect_container_info (std::string const &);
};
} // namespace nano::scheduler
