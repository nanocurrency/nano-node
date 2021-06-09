#pragma once

#include <nano/lib/numbers.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <stack>
#include <unordered_set>

namespace nano
{
class block;
class ledger;
class transaction;

/** Walks the ledger starting from a start block and applying a depth-first search algorithm */
class ledger_walker final
{
public:
	using visitor_callback = std::function<bool (std::shared_ptr<nano::block> const &)>;

	explicit ledger_walker (nano::ledger const & ledger_a);
	/** Start traversing (in a backwards direction -- towards genesis) from \p start_block_a until \p visitor_callback_a returns false */
	void walk_backward (nano::block_hash const & start_block_a, visitor_callback const & visitor_callback_a);
	static constexpr std::size_t in_memory_block_count = 65536;

private:
	nano::ledger const & ledger;
	std::unordered_set<nano::block_hash> walked_blocks;
	std::stack<nano::block_hash> blocks_to_walk;

	void enqueue_block (nano::block_hash block_a);
	void enqueue_block (std::shared_ptr<nano::block> const & block_a);
	std::shared_ptr<nano::block> dequeue_block (nano::transaction const & transaction_a);

	friend class ledger_walker_genesis_account_longer_Test;
};

}
