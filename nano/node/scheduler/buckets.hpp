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
	std::vector<std::unique_ptr<bucket>> buckets_m;

	/** index of bucket to read next */
	decltype (buckets_m)::const_iterator current;

	/** maximum number of blocks in whole container, each bucket's maximum is maximum / bucket_number */
	uint64_t const maximum;

	void next ();
	void seek ();
	void setup_buckets (uint64_t maximum);

public:
	buckets (uint64_t maximum = 250000u);
	~buckets ();
	// Returns true if the block was inserted
	bool push (uint64_t time, std::shared_ptr<nano::block> block, nano::amount const & priority);
	std::shared_ptr<nano::block> top () const;
	void pop ();
	std::size_t size () const;
	std::size_t bucket_count () const;
	std::size_t bucket_size (std::size_t index) const;
	bool empty () const;
	void dump () const;
	bucket & find_bucket (nano::uint128_t priority);

	std::unique_ptr<nano::container_info_component> collect_container_info (std::string const &);
};
} // namespace nano::scheduler
