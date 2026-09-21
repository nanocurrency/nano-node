#include <nano/benchmarks/allocations.hpp>

#include <cstdlib>
#include <new>

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
