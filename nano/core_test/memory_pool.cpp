#include <celerix/lib/blocks.hpp>
#include <celerix/lib/memory.hpp>
#include <celerix/node/active_elections.hpp>
#include <celerix/secure/common.hpp>
#include <celerix/secure/vote.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace
{
/** This allocator records the size of all allocations that happen */
template <class T>
class record_allocations_new_delete_allocator
{
public:
	using value_type = T;

	explicit record_allocations_new_delete_allocator (std::vector<size_t> * allocated) :
		allocated (allocated)
	{
	}

	template <typename U>
	record_allocations_new_delete_allocator (const record_allocations_new_delete_allocator<U> & a)
	{
		allocated = a.allocated;
	}

	template <typename U>
	record_allocations_new_delete_allocator & operator= (const record_allocations_new_delete_allocator<U> &) = delete;

	T * allocate (size_t num_to_allocate)
	{
		auto size_allocated = (sizeof (T) * num_to_allocate);
		allocated->push_back (size_allocated);
		return static_cast<T *> (::operator new (size_allocated));
	}

	void deallocate (T * p, size_t num_to_deallocate)
	{
		::operator delete (p);
	}

	std::vector<size_t> * allocated;
};

template <typename T>
size_t get_allocated_size ()
{
	std::vector<size_t> allocated;
	record_allocations_new_delete_allocator<T> alloc (&allocated);
	(void)std::allocate_shared<T, record_allocations_new_delete_allocator<T>> (alloc);
	debug_assert (allocated.size () == 1);
	return allocated.front ();
}
}

TEST (memory_pool, validate_cleanup)
{
	// This might be turned off, e.g on Mac for instance, so don't do this test
	if (!celerix::get_use_memory_pools ())
	{
		return;
	}

	celerix::make_shared<celerix::open_block> ();
	celerix::make_shared<celerix::receive_block> ();
	celerix::make_shared<celerix::send_block> ();
	celerix::make_shared<celerix::change_block> ();
	celerix::make_shared<celerix::state_block> ();
	celerix::make_shared<celerix::vote> ();

	ASSERT_TRUE (celerix::purge_shared_ptr_singleton_pool_memory<celerix::open_block> ());
	ASSERT_TRUE (celerix::purge_shared_ptr_singleton_pool_memory<celerix::receive_block> ());
	ASSERT_TRUE (celerix::purge_shared_ptr_singleton_pool_memory<celerix::send_block> ());
	ASSERT_TRUE (celerix::purge_shared_ptr_singleton_pool_memory<celerix::state_block> ());
	ASSERT_TRUE (celerix::purge_shared_ptr_singleton_pool_memory<celerix::vote> ());

	// Change blocks have the same size as open_block so won't deallocate any memory
	ASSERT_FALSE (celerix::purge_shared_ptr_singleton_pool_memory<celerix::change_block> ());

	ASSERT_EQ (celerix::determine_shared_ptr_pool_size<celerix::open_block> (), get_allocated_size<celerix::open_block> () - sizeof (size_t));
	ASSERT_EQ (celerix::determine_shared_ptr_pool_size<celerix::receive_block> (), get_allocated_size<celerix::receive_block> () - sizeof (size_t));
	ASSERT_EQ (celerix::determine_shared_ptr_pool_size<celerix::send_block> (), get_allocated_size<celerix::send_block> () - sizeof (size_t));
	ASSERT_EQ (celerix::determine_shared_ptr_pool_size<celerix::change_block> (), get_allocated_size<celerix::change_block> () - sizeof (size_t));
	ASSERT_EQ (celerix::determine_shared_ptr_pool_size<celerix::state_block> (), get_allocated_size<celerix::state_block> () - sizeof (size_t));
	ASSERT_EQ (celerix::determine_shared_ptr_pool_size<celerix::vote> (), get_allocated_size<celerix::vote> () - sizeof (size_t));
}
