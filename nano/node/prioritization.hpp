#pragma once
#include <nano/lib/numbers.hpp>
#include <nano/lib/utility.hpp>

#include <cstddef>
#include <set>
#include <vector>

namespace nano
{
class block;
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
	std::vector<priority> buckets;
	std::vector<nano::uint128_t> minimums;
	/** maximum number of blocks in whole container, each bucket's maximum is maximum / bucket_number */
	uint64_t const maximum;

	void next ();
	void seek ();
	void populate_schedule ();
	// Contains bucket indicies to iterate over when making the next scheduling decision
	std::vector<uint8_t> schedule;
	decltype (schedule)::const_iterator current;

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

	std::unique_ptr<nano::container_info_component> collect_container_info (std::string const &);
};
}
