// TODO: keep this until diskhash builds fine on Windows
#ifndef _WIN32

#pragma once

#include <nano/lib/numbers.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <stack>
#include <unordered_set>

#include <diskhash.hpp>

namespace nano::store
{
class transaction;
}

namespace nano
{
class block;
class ledger;

/** Walks the ledger starting from a start block and applying a depth-first search algorithm */
class ledger_walker final
{
public:
	using should_visit_callback = std::function<bool (std::shared_ptr<nano::block> const &)>;
	using visitor_callback = std::function<void (std::shared_ptr<nano::block> const &)>;

	explicit ledger_walker (nano::ledger const & ledger_a);

	/** Start traversing (in a backwards direction -- towards genesis) from \p start_block_hash_a until \p should_visit_callback_a returns false, calling \p visitor_callback_a at each block. Prefer 'walk' instead, if possible. */
	void walk_backward (nano::block_hash const & start_block_hash_a, should_visit_callback const & should_visit_callback_a, visitor_callback const & visitor_callback_a);

	/** Start traversing (in a forward direction -- towards end_block_hash_a) from first block (genesis onwards) where \p should_visit_a returns true until \p end_block_hash_a, calling \p visitor_callback at each block. Prefer this one, instead of 'walk_backwards', if possible. */
	void walk (nano::block_hash const & end_block_hash_a, should_visit_callback const & should_visit_callback_a, visitor_callback const & visitor_callback_a);

	/** Methods similar to walk_backward and walk, but that do not offer the possibility of providing a user-defined should_visit_callback function. */
	void walk_backward (nano::block_hash const & start_block_hash_a, visitor_callback const & visitor_callback_a);
	void walk (nano::block_hash const & end_block_hash_a, visitor_callback const & visitor_callback_a);

	/** How many blocks will be held in the in-memory hash before using the disk hash for walking. */
	// TODO TSB: make this 65536
	static constexpr std::size_t in_memory_block_count = 0;

private:
	nano::ledger const & ledger;
	bool use_in_memory_walked_blocks;
	std::unordered_set<nano::block_hash> walked_blocks;
	std::optional<dht::DiskHash<bool>> walked_blocks_disk;
	std::stack<nano::block_hash> blocks_to_walk;

	void enqueue_block (nano::block_hash block_hash_a);
	void enqueue_block (std::shared_ptr<nano::block> const & block_a);
	bool add_to_walked_blocks (nano::block_hash const & block_hash_a);
	bool add_to_walked_blocks_disk (nano::block_hash const & block_hash_a);
	void clear_queue ();
	std::shared_ptr<nano::block> dequeue_block (store::transaction const & transaction_a);

	friend class ledger_walker_genesis_account_longer_Test;
};

}

#endif // _WIN32 -- TODO: keep this until diskhash builds fine on Windows
