#pragma once
#include <nano/lib/numbers.hpp>

#include <cstddef>
#include <map>

namespace nano
{
class block;
class prioritization final
{
	class value_type
	{
	public:
		uint32_t time;
		nano::block_hash hash;
		bool operator< (value_type const & other_a) const
		{
			return time < other_a.time || hash < other_a.hash;
		}
	};
	static void drop_void (nano::block_hash const &) {};
	using priority = std::set<value_type>;
	std::array<priority, 128> buckets;
	std::array<nano::uint128_t, 128> minimums;
	void trim ();
	std::function<void (nano::block_hash const &)> const & drop;
public:
	prioritization (std::function<void (nano::block_hash const &)> const & drop_a = drop_void);
	void insert (uint32_t time, nano::amount const & balance_a, nano::block_hash const & hash_a);
	size_t size () const;
	size_t bucket_count () const { return buckets.size (); }
	size_t bucket_size (size_t index) const { return buckets[index].size (); }
	static size_t constexpr unconfirmed_max = 250000;
};
}
