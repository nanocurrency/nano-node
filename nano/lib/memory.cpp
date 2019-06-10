#include <nano/lib/memory.hpp>

bool nano::use_memory_pools{ true };

nano::cleanup_guard::cleanup_guard (std::vector<std::function<void()>> const & cleanup_funcs_a) :
cleanup_funcs (cleanup_funcs_a)
{
}

nano::cleanup_guard::~cleanup_guard ()
{
	for (auto & func : cleanup_funcs)
	{
		func ();
	}
}
