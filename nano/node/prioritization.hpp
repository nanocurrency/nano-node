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
	std::array<priority, 128> buckets;
	std::array<nano::uint128_t, 128> minimums;
	void trim ();
	void next ();
	void populate_schedule ();
	std::function<void (nano::block_hash const &)> const & drop;
	// Contains bucket indicies to iterate over when making the next scheduling decision
	std::vector<uint8_t> schedule;
	decltype(schedule)::const_iterator current;
public:
	prioritization (std::function<void (nano::block_hash const &)> const & drop_a = drop_void);
	void insert (uint32_t time, nano::amount const & balance_a, nano::account const & account_a);
	template <typename filter>
	nano::account fetch (filter const & filter_a)
	{
		nano::account result{ 0 };
		for (auto count = 0; count < buckets.size () && result.is_zero (); ++count, next ())
		{
			for (auto i = buckets[*current].begin (), n = buckets[*current].end (); i != n && result.is_zero (); ++i)
			{
				auto time = i->time;
				auto account = i->account;
				if (filter_a.find (account) == filter_a.end ())
				{
					result = account;
				}
			}
		}
		return result;
	}
	size_t size () const;
	size_t bucket_count () const { return buckets.size (); }
	size_t bucket_size (size_t index) const { return buckets[index].size (); }
	static size_t constexpr unconfirmed_max = 250000;
};
}
