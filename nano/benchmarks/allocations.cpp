#include <nano/benchmarks/allocations.hpp>

#include <cstdlib>
#include <new>
#include <string>

// A sanitizer runtime defines operator new itself and the linker rejects a second definition
#if defined(__has_feature)
#if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
#define NANO_BENCHMARKS_SANITIZED 1
#endif
#elif defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__)
#define NANO_BENCHMARKS_SANITIZED 1
#endif

namespace
{
// Constant initialized, so reading them never allocates
thread_local uint64_t allocation_count{ 0 };
thread_local uint64_t allocation_bytes{ 0 };
}

nano::benchmarks::allocation_stats nano::benchmarks::thread_allocations ()
{
	return { allocation_count, allocation_bytes };
}

nano::benchmarks::allocation_counter::allocation_counter (std::string_view unit_a) :
	unit{ unit_a }
{
}

void nano::benchmarks::allocation_counter::report (benchmark::State & state) const
{
	auto const used = thread_allocations () - start;
	auto const iterations = static_cast<double> (state.iterations ());
	std::string const unit_l{ unit };
	state.counters["allocs/" + unit_l] = benchmark::Counter (used.count / iterations, benchmark::Counter::kAvgThreads);
	state.counters["bytes/" + unit_l] = benchmark::Counter (used.bytes / iterations, benchmark::Counter::kAvgThreads);
	// Rates are divided by the summed up time of all threads, scaling by the thread count turns that into the total throughput
	state.counters[unit_l + "s/s"] = benchmark::Counter (iterations * state.threads (), benchmark::Counter::kIsRate);
}

#ifndef NANO_BENCHMARKS_SANITIZED

/*
 * Replacing the plain forms is enough: the default array and nothrow forms forward to them.
 * The aligned forms keep their defaults and stay uncounted, which keeps allocation and release paired on every platform.
 */
void * operator new (std::size_t size)
{
	++allocation_count;
	allocation_bytes += size;
	if (auto * result = std::malloc (size != 0 ? size : 1))
	{
		return result;
	}
	throw std::bad_alloc{};
}

void operator delete (void * pointer) noexcept
{
	std::free (pointer);
}

void operator delete (void * pointer, std::size_t) noexcept
{
	std::free (pointer);
}

#endif
