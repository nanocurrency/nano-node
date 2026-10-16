#pragma once

#include <cstdint>
#include <string_view>

#include <benchmark/benchmark.h>

namespace nano::benchmarks
{
/**
 * Heap allocations made through operator new, counted per thread.
 * Allocations made directly through malloc are not seen, and the counts stay at zero in sanitizer builds, where the sanitizer runtime owns operator new.
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

/**
 * Measures the allocations of the calling thread from construction until report, per iteration and as a rate.
 * `unit` names what one iteration does, so a unit of "vote" reports allocs/vote, bytes/vote and votes/s.
 */
class allocation_counter final
{
public:
	explicit allocation_counter (std::string_view unit);

	// Sets the benchmark's allocation and rate counters for the iterations run so far
	void report (benchmark::State &) const;

private:
	std::string_view unit; // What one iteration does, it names the counters
	allocation_stats const start{ thread_allocations () }; // Allocations of the thread at construction
};
}
