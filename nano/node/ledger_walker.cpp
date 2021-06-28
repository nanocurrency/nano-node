#include <nano/lib/blocks.hpp>
#include <nano/lib/errors.hpp>
#include <nano/node/ledger_walker.hpp>
#include <nano/secure/blockstore.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/utility.hpp>

#include <utility>

nano::ledger_walker::ledger_walker (nano::ledger const & ledger_a) :
	ledger{ ledger_a },
	walked_blocks{},
	walked_blocks_disk{},
	blocks_to_walk{}
{
	debug_assert (!ledger.store.init_error ());
}

void nano::ledger_walker::walk_backward (nano::block_hash const & start_block_hash_a, should_visit_callback const & should_visit_callback_a, visitor_callback const & visitor_callback_a)
{
	debug_assert (!walked_blocks_disk.has_value ());
	walked_blocks_disk.emplace (nano::unique_path ().c_str (), sizeof (nano::block_hash::bytes) + 1, dht::DHOpenRW);

	const auto transaction = ledger.store.tx_begin_read ();

	enqueue_block (start_block_hash_a);
	while (!blocks_to_walk.empty ())
	{
		const auto block = dequeue_block (transaction);
		if (!should_visit_callback_a (block))
		{
			continue;
		}

		visitor_callback_a (block);
		for (const auto & hash : ledger.dependent_blocks (transaction, *block))
		{
			if (!hash.is_zero ())
			{
				enqueue_block (ledger.store.block_get (transaction, hash));
			}
		}
	}

	clear_queue ();
}

void nano::ledger_walker::walk (nano::block_hash const & end_block_hash_a, should_visit_callback const & should_visit_callback_a, visitor_callback const & visitor_callback_a)
{
	std::uint64_t last_walked_block_order_index = 0;
	dht::DiskHash<nano::block_hash> walked_blocks_order{ nano::unique_path ().c_str (), sizeof (std::uint64_t) + 1, dht::DHOpenRW };

	walk_backward (end_block_hash_a, should_visit_callback_a, [&] (const auto & block) {
		walked_blocks_order.insert (std::to_string (++last_walked_block_order_index).c_str (), block->hash ());
	});

	const auto transaction = ledger.store.tx_begin_read ();
	for (auto walked_block_order_index = last_walked_block_order_index; walked_block_order_index != 0; --walked_block_order_index)
	{
		const auto * block_hash = walked_blocks_order.lookup (std::to_string (walked_block_order_index).c_str ());
		if (!block_hash)
		{
			std::abort ();
			continue;
		}

		const auto block = ledger.store.block_get (transaction, *block_hash);
		if (!block)
		{
			std::abort ();
			continue;
		}

		visitor_callback_a (block);
	}
}

void nano::ledger_walker::enqueue_block (nano::block_hash block_hash_a)
{
	if (add_to_walked_blocks (block_hash_a))
	{
		blocks_to_walk.emplace (std::move (block_hash_a));
	}
}

void nano::ledger_walker::enqueue_block (std::shared_ptr<nano::block> const & block_a)
{
	if (block_a)
	{
		enqueue_block (block_a->hash ());
	}
}

bool nano::ledger_walker::add_to_walked_blocks (nano::block_hash const & block_hash_a)
{
	static constexpr bool use_diskhash = true;
	if (use_diskhash)
	{
		std::array<decltype (nano::block_hash::chars)::value_type, sizeof (nano::block_hash::chars) + 1> block_hash_key{};
		std::copy (block_hash_a.chars.cbegin (),
		block_hash_a.chars.cend (),
		block_hash_key.begin ());

		return walked_blocks_disk->insert (block_hash_key.data (), true);
	}

	return walked_blocks.emplace (block_hash_a).second;
}

void nano::ledger_walker::clear_queue ()
{
	decltype (walked_blocks){}.swap (walked_blocks);
	walked_blocks_disk.reset ();
	decltype (blocks_to_walk){}.swap (blocks_to_walk);
}

std::shared_ptr<nano::block> nano::ledger_walker::dequeue_block (nano::transaction const & transaction_a)
{
	auto block = ledger.store.block_get (transaction_a, blocks_to_walk.top ());
	blocks_to_walk.pop ();

	return block;
}
