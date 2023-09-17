// TODO: keep this until diskhash builds fine on Windows
#ifndef _WIN32

#include <nano/lib/blocks.hpp>
#include <nano/lib/errors.hpp>
#include <nano/node/ledger_walker.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/utility.hpp>
#include <nano/store/component.hpp>

#include <algorithm>
#include <limits>
#include <utility>

nano::ledger_walker::ledger_walker (nano::ledger const & ledger_a) :
	ledger{ ledger_a },
	use_in_memory_walked_blocks{ true },
	walked_blocks{},
	walked_blocks_disk{},
	blocks_to_walk{}
{
	debug_assert (!ledger.store.init_error ());
}

void nano::ledger_walker::walk_backward (nano::block_hash const & start_block_hash_a, should_visit_callback const & should_visit_callback_a, visitor_callback const & visitor_callback_a)
{
	auto const transaction = ledger.store.tx_begin_read ();

	enqueue_block (start_block_hash_a);
	while (!blocks_to_walk.empty ())
	{
		auto const block = dequeue_block (transaction);
		if (!should_visit_callback_a (block))
		{
			continue;
		}

		visitor_callback_a (block);
		for (auto const & hash : ledger.dependent_blocks (transaction, *block))
		{
			if (!hash.is_zero ())
			{
				auto const dependent_block = ledger.store.block.get (transaction, hash);
				if (dependent_block)
				{
					enqueue_block (dependent_block);
				}
			}
		}
	}

	clear_queue ();
}

void nano::ledger_walker::walk (nano::block_hash const & end_block_hash_a, should_visit_callback const & should_visit_callback_a, visitor_callback const & visitor_callback_a)
{
	std::uint64_t last_walked_block_order_index = 0;
	dht::DiskHash<nano::block_hash> walked_blocks_order{ nano::unique_path ().c_str (), static_cast<int> (std::to_string (std::numeric_limits<std::uint64_t>::max ()).size ()) + 1, dht::DHOpenRW };

	walk_backward (end_block_hash_a,
	should_visit_callback_a,
	[&] (auto const & block) {
		walked_blocks_order.insert (std::to_string (++last_walked_block_order_index).c_str (), block->hash ());
	});

	auto const transaction = ledger.store.tx_begin_read ();
	for (auto walked_block_order_index = last_walked_block_order_index; walked_block_order_index != 0; --walked_block_order_index)
	{
		auto const * block_hash = walked_blocks_order.lookup (std::to_string (walked_block_order_index).c_str ());
		if (!block_hash)
		{
			debug_assert (false);
			continue;
		}

		auto const block = ledger.store.block.get (transaction, *block_hash);
		if (!block)
		{
			debug_assert (false);
			continue;
		}

		visitor_callback_a (block);
	}
}

void nano::ledger_walker::walk_backward (nano::block_hash const & start_block_hash_a, visitor_callback const & visitor_callback_a)
{
	walk_backward (
	start_block_hash_a,
	[&] (auto const & /* block */) {
		return true;
	},
	visitor_callback_a);
}

void nano::ledger_walker::walk (nano::block_hash const & end_block_hash_a, visitor_callback const & visitor_callback_a)
{
	walk (
	end_block_hash_a,
	[&] (auto const & /* block */) {
		return true;
	},
	visitor_callback_a);
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
	debug_assert (block_a);
	enqueue_block (block_a->hash ());
}

bool nano::ledger_walker::add_to_walked_blocks (nano::block_hash const & block_hash_a)
{
	if (use_in_memory_walked_blocks)
	{
		if (walked_blocks.size () < in_memory_block_count)
		{
			return walked_blocks.emplace (block_hash_a).second;
		}

		use_in_memory_walked_blocks = false;

		debug_assert (!walked_blocks_disk.has_value ());
		walked_blocks_disk.emplace (nano::unique_path ().c_str (), sizeof (nano::block_hash::bytes) + 1, dht::DHOpenRW);

		for (auto const & walked_block_hash : walked_blocks)
		{
			if (!add_to_walked_blocks_disk (walked_block_hash))
			{
				debug_assert (false);
			}
		}

		decltype (walked_blocks){}.swap (walked_blocks);
	}

	return add_to_walked_blocks_disk (block_hash_a);
}

bool nano::ledger_walker::add_to_walked_blocks_disk (nano::block_hash const & block_hash_a)
{
	debug_assert (!use_in_memory_walked_blocks);
	debug_assert (walked_blocks_disk.has_value ());

	std::array<decltype (nano::block_hash::chars)::value_type, sizeof (nano::block_hash::bytes) + 1> block_hash_key{};
	std::copy (block_hash_a.chars.cbegin (),
	block_hash_a.chars.cend (),
	block_hash_key.begin ());

	return walked_blocks_disk->insert (block_hash_key.data (), true);
}

void nano::ledger_walker::clear_queue ()
{
	use_in_memory_walked_blocks = true;

	decltype (walked_blocks){}.swap (walked_blocks);
	walked_blocks_disk.reset ();

	decltype (blocks_to_walk){}.swap (blocks_to_walk);
}

std::shared_ptr<nano::block> nano::ledger_walker::dequeue_block (nano::transaction const & transaction_a)
{
	auto block = ledger.store.block.get (transaction_a, blocks_to_walk.top ());
	blocks_to_walk.pop ();

	return block;
}

#endif // _WIN32 -- TODO: keep this until diskhash builds fine on Windows
