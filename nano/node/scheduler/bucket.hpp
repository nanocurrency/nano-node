#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>

namespace nano
{
class block;
}
namespace nano::scheduler
{
/** A class which holds an ordered set of blocks to be scheduled, ordered by their block arrival time
 * Tracks two internal counters limited by 'maximum'
 * 1) active - number of elections this bucket has been responsible for starting
 * 2) queue - A std::set ordered from highest to lowest priority which drops the last, lowest priority item
 */
class bucket final
{
	class value_type
	{
	public:
		uint64_t time;
		std::shared_ptr<nano::block> block;
		bool operator< (value_type const & other_a) const;
		bool operator== (value_type const & other_a) const;
	};
	std::set<value_type> queue;

public:
	bucket (size_t maximum);
	~bucket ();
	std::shared_ptr<nano::block> top () const;
	void pop ();
	void push (uint64_t time, std::shared_ptr<nano::block> block);
	size_t size () const;
	bool empty () const;
	void dump () const;

	// Tracks the number of active elections started by this bucket
	size_t active{ 0 };
	size_t const maximum;
};
} // namespace nano::scheduler
