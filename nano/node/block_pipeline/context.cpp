#include <nano/node/block_pipeline/context.hpp>

nano::account const nano::block_pipeline::context:: account_one = { 1u };

bool nano::block_pipeline::context::is_send () const
{
	debug_assert (state.has_value ());
	auto legacy_send = block->type () == nano::block_type::send;
	auto type = block->type () == nano::block_type::state;
	auto decreased = block->balance () < state->balance;
	return legacy_send || (type && decreased);
}

nano::account nano::block_pipeline::context::account () const
{
	switch (block->type ())
	{
		case nano::block_type::change:
		case nano::block_type::receive:
		case nano::block_type::send:
			debug_assert (previous != nullptr);
			switch (previous->type ())
			{
				case nano::block_type::state:
				case nano::block_type::open:
					return previous->account ();
				case nano::block_type::change:
				case nano::block_type::receive:
				case nano::block_type::send:
					return previous->sideband ().account;
				case nano::block_type::not_a_block:
				case nano::block_type::invalid:
					debug_assert (false);
					break;
			}
			break;
		case nano::block_type::state:
		case nano::block_type::open:
			return block->account ();
		case nano::block_type::not_a_block:
		case nano::block_type::invalid:
			debug_assert (false);
			break;
	}
	// std::unreachable (); c++23
	return 1; // Return an account that cannot be signed for.
}

nano::block_hash nano::block_pipeline::context::source () const
{
	switch (block->type ())
	{
		case nano::block_type::send:
		case nano::block_type::change:
			// 0 is returned for source on send/change blocks
		case nano::block_type::receive:
		case nano::block_type::open:
			return block->source ();
		case nano::block_type::state:
			return block->link ().as_block_hash ();
		case nano::block_type::not_a_block:
		case nano::block_type::invalid:
			return 0;
	}
	debug_assert (false);
	return 0;
}

nano::account const & nano::block_pipeline::context::signer (nano::epochs const & epochs) const
{
	debug_assert (block != nullptr);
	switch (block->type ())
	{
		case nano::block_type::send:
		case nano::block_type::receive:
		case nano::block_type::change:
			debug_assert (previous != nullptr); // Previous block must be passed in for non-open blocks
			switch (previous->type ())
			{
				case nano::block_type::state:
					debug_assert (false && "Legacy blocks can't follow state blocks");
					break;
				case nano::block_type::open:
					// Open blocks have the account written in the block.
					return previous->account ();
				default:
					// Other legacy block types have the account stored in sideband.
					return previous->sideband ().account;
			}
			break;
		case nano::block_type::state:
		{
			debug_assert (dynamic_cast<nano::state_block *> (block.get ()));
			// If the block is a send, while the link field may contain an epoch link value, it is actually a malformed destination address.
			return (!epochs.is_epoch_link (block->link ()) || is_send ()) ? block->account () : epochs.signer (epochs.epoch (block->link ()));
		}
		case nano::block_type::open: // Open block signer is determined statelessly as it's written in the block
			return block->account ();
		case nano::block_type::invalid:
		case nano::block_type::not_a_block:
			debug_assert (false);
			break;
	}
	// std::unreachable (); c++23
	return account_one; // Return an account that cannot be signed for.
}

bool nano::block_pipeline::context::gap_previous () const
{
	return !block->previous ().is_zero () && previous == nullptr;
}
