#pragma once
#include <nano/lib/numbers.hpp>

#include <cstddef>
#include <map>
#include <vector>

namespace nano
{
class block;
class prioritization final
{
	class value_type
	{
	public:
		uint32_t time;
		nano::account account;
		bool operator< (value_type const & other_a) const
		{
			return time < other_a.time || account < other_a.account;
		}
	};
	static void drop_void (nano::block_hash const &) {};
	using priority = std::set<value_type>;
	std::vector<priority> buckets;
	std::vector<nano::uint128_t> minimums;
	void next ();
	void seek ();
	void populate_schedule ();
	std::function<void (nano::block_hash const &)> const & drop;
	// Contains bucket indicies to iterate over when making the next scheduling decision
	std::vector<uint8_t> schedule;
	decltype(schedule)::const_iterator current;
public:
	prioritization (std::function<void (nano::block_hash const &)> const & drop_a = drop_void);
	void push (uint32_t time, nano::amount const & balance_a, nano::account const & account_a);
	nano::account top () const;
	void pop ();
	size_t size () const;
	size_t bucket_count () const;
	size_t bucket_size (size_t index) const;
	bool empty () const;
	static size_t constexpr unconfirmed_max = 250000;
};
}
