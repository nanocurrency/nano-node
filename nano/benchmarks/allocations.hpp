#pragma once

#include <cstdint>

namespace nano::benchmarks
{
/**
 * Heap allocations made through operator new, counted per thread.
 * Allocations made directly through malloc are not seen.
 */
struct allocation_stats
{
	uint64_t count{ 0 }; // Number of allocations
	uint64_t bytes{ 0 }; // Sum of the requested sizes

	allocation_stats operator- (allocation_stats const & other) const
	{
		return { count - other.count, bytes - other.bytes };
	}
};

// Allocations the calling thread has made since it started
allocation_stats thread_allocations ();
}
