#pragma once

#include <nano/lib/jsonconfig.hpp>
#include <nano/lib/memory.hpp>
#include <nano/node/transport/common.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/network_filter.hpp>

#include <bitset>
#include <optional>

namespace nano
{
/** Helper guard which contains all the necessary purge (remove all memory even if used) functions */
class node_singleton_memory_pool_purge_guard
{
public:
	node_singleton_memory_pool_purge_guard ();

private:
	nano::cleanup_guard cleanup_guard;
};
}
