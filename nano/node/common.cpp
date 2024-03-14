#include <nano/lib/blocks.hpp>
#include <nano/lib/memory.hpp>
#include <nano/lib/stream.hpp>
#include <nano/node/active_transactions.hpp>
#include <nano/node/common.hpp>
#include <nano/node/election.hpp>
#include <nano/node/network.hpp>
#include <nano/node/wallet.hpp>

nano::node_singleton_memory_pool_purge_guard::node_singleton_memory_pool_purge_guard () :
	cleanup_guard ({ nano::block_memory_pool_purge, nano::purge_shared_ptr_singleton_pool_memory<nano::vote>, nano::purge_shared_ptr_singleton_pool_memory<nano::election> })
{
}
