#pragma once
#include <nano/lib/numbers.hpp>
#include <nano/lib/utility.hpp>

#include <cstddef>
#include <set>
#include <vector>

namespace nano
{
class block;

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
class prioritization final
{
	class value_type
	{
	public:
		uint64_t time;
		std::shared_ptr<nano::block> block;
		bool operator< (value_type const & other_a) const;
		bool operator== (value_type const & other_a) const;
	};

	using priority = std::set<value_type>;

	/** container for the buckets to be read in round robin fashion */
	std::vector<priority> buckets;

	/** thresholds that define the bands for each bucket, the minimum balance an account must have to enter a bucket,
	 *  the container writes a block to the lowest indexed bucket that has balance larger than the bucket's minimum value */
	std::vector<nano::uint128_t> minimums;

	/** Contains bucket indicies to iterate over when making the next scheduling decision */
	std::vector<uint8_t> schedule;

	/** index of bucket to read next */
	decltype (schedule)::const_iterator current;

	/** maximum number of blocks in whole container, each bucket's maximum is maximum / bucket_number */
	uint64_t const maximum;

	void next ();
	void seek ();
	void populate_schedule ();

public:
	prioritization (uint64_t maximum = 250000u);
	void push (uint64_t time, std::shared_ptr<nano::block> block);
	std::shared_ptr<nano::block> top () const;
	void pop ();
	std::size_t size () const;
	std::size_t bucket_count () const;
	std::size_t bucket_size (std::size_t index) const;
	bool empty () const;
	void dump () const;
	std::size_t index (nano::uint128_t const & balance) const;

	std::unique_ptr<nano::container_info_component> collect_container_info (std::string const &);
};
}
