#include <nano/lib/blocks.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/utility.hpp>
#include <nano/lib/work.hpp>
#include <nano/node/make_store.hpp>
#include <nano/secure/block_check_context.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/secure/ledger_set_confirmed.hpp>
#include <nano/secure/rep_weights.hpp>
#include <nano/store/account.hpp>
#include <nano/store/block.hpp>
#include <nano/store/component.hpp>
#include <nano/store/confirmation_height.hpp>
#include <nano/store/final.hpp>
#include <nano/store/online_weight.hpp>
#include <nano/store/peer.hpp>
#include <nano/store/pending.hpp>
#include <nano/store/pruned.hpp>
#include <nano/store/rep_weight.hpp>
#include <nano/store/rocksdb/unconfirmed_set.hpp>
#include <nano/store/version.hpp>

#include <stack>

#include <cryptopp/words.h>

namespace
{
/**
 * Determine the representative for this block
 */
class representative_visitor final : public nano::block_visitor
{
public:
	representative_visitor (nano::secure::transaction const & transaction_a, nano::ledger & ledger);
	~representative_visitor () = default;
	void compute (nano::block_hash const & hash_a);
	void send_block (nano::send_block const & block_a) override;
	void receive_block (nano::receive_block const & block_a) override;
	void open_block (nano::open_block const & block_a) override;
	void change_block (nano::change_block const & block_a) override;
	void state_block (nano::state_block const & block_a) override;
	nano::secure::transaction const & transaction;
	nano::ledger & ledger;
	nano::block_hash current;
	nano::block_hash result;
};

representative_visitor::representative_visitor (nano::secure::transaction const & transaction_a, nano::ledger & ledger) :
	transaction{ transaction_a },
	ledger{ ledger },
	result{ 0 }
{
}

void representative_visitor::compute (nano::block_hash const & hash_a)
{
	current = hash_a;
	while (result.is_zero ())
	{
		auto block_l = ledger.any.block_get (transaction, current);
		debug_assert (block_l != nullptr);
		block_l->visit (*this);
	}
}

void representative_visitor::send_block (nano::send_block const & block_a)
{
	current = block_a.previous ();
}

void representative_visitor::receive_block (nano::receive_block const & block_a)
{
	current = block_a.previous ();
}

void representative_visitor::open_block (nano::open_block const & block_a)
{
	result = block_a.hash ();
}

void representative_visitor::change_block (nano::change_block const & block_a)
{
	result = block_a.hash ();
}

void representative_visitor::state_block (nano::state_block const & block_a)
{
	result = block_a.hash ();
}
} // namespace

nano::ledger::ledger (nano::store::component & store_a, nano::stats & stat_a, nano::ledger_constants & constants, nano::generate_cache_flags const & generate_cache_flags_a, nano::uint128_t min_rep_weight_a) :
	constants{ constants },
	store{ store_a },
	stats{ stat_a },
	check_bootstrap_weights{ true },
	any_impl{ std::make_unique<ledger_set_any> (*this) },
	confirmed_impl{ std::make_unique<ledger_set_confirmed> (*this) },
	unconfirmed_impl{ std::make_unique<nano::store::unconfirmed_set> () },
	any{ *any_impl },
	confirmed{ *confirmed_impl },
	unconfirmed{ *unconfirmed_impl },
	cache{ store.rep_weight, unconfirmed.rep_weight, min_rep_weight_a }
{
	if (!store.init_error ())
	{
		initialize (generate_cache_flags_a);
	}
}

nano::ledger::~ledger ()
{
}

auto nano::ledger::tx_begin_write (nano::store::writer guard_type) const -> secure::write_transaction
{
	auto guard = store.write_queue.wait (guard_type);
	auto txn = store.tx_begin_write ();
	auto unconfirmed_txn = unconfirmed.tx_begin_write ();
	return secure::write_transaction{ std::move (txn), std::move (unconfirmed_txn), std::move (guard) };
}

auto nano::ledger::tx_begin_read () const -> secure::read_transaction
{
	return secure::read_transaction{ store.tx_begin_read (), unconfirmed.tx_begin_read () };
}

void nano::ledger::initialize (nano::generate_cache_flags const & generate_cache_flags_a)
{
	if (generate_cache_flags_a.reps || generate_cache_flags_a.account_count || generate_cache_flags_a.block_count)
	{
		store.account.for_each_par (
		[this] (store::read_transaction const & /*unused*/, store::iterator<nano::account, nano::account_info> i, store::iterator<nano::account, nano::account_info> n) {
			uint64_t block_count_l{ 0 };
			uint64_t account_count_l{ 0 };
			for (; i != n; ++i)
			{
				nano::account_info const & info (i->second);
				block_count_l += info.block_count;
				++account_count_l;
			}
			this->cache.block_count += block_count_l;
			this->cache.account_count += account_count_l;
		});

		store.rep_weight.for_each_par (
		[this] (store::read_transaction const & /*unused*/, store::iterator<nano::account, nano::uint128_union> i, store::iterator<nano::account, nano::uint128_union> n) {
			nano::rep_weights rep_weights_l{ this->store.rep_weight, this->unconfirmed.rep_weight };
			for (; i != n; ++i)
			{
				rep_weights_l.representation_put (i->first, i->second.number ());
			}
			this->cache.rep_weights.copy_from (rep_weights_l);
		});
	}

	if (generate_cache_flags_a.cemented_count)
	{
		store.confirmation_height.for_each_par (
		[this] (store::read_transaction const & /*unused*/, store::iterator<nano::account, nano::confirmation_height_info> i, store::iterator<nano::account, nano::confirmation_height_info> n) {
			uint64_t cemented_count_l (0);
			for (; i != n; ++i)
			{
				cemented_count_l += i->second.height;
			}
			this->cache.cemented_count += cemented_count_l;
		});
	}

	auto transaction (store.tx_begin_read ());
	cache.pruned_count = store.pruned.count (transaction);
}

nano::uint128_t nano::ledger::account_receivable (secure::transaction const & transaction_a, nano::account const & account_a, bool only_confirmed_a)
{
	nano::uint128_t result{ 0 };
	for (auto i = any.receivable_upper_bound (transaction_a, account_a, 0), n = any.receivable_end (); i != n; ++i)
	{
		auto const & [key, info] = *i;
		if (!only_confirmed_a || confirmed.block_exists_or_pruned (transaction_a, key.hash))
		{
			result += info.amount.number ();
		}
	}
	return result;
}

// Both stack and result set are bounded to limit maximum memory usage
// Callers must ensure that the target block was confirmed, and if not, call this function multiple times
std::deque<std::shared_ptr<nano::block>> nano::ledger::confirm (secure::write_transaction & transaction, nano::block_hash const & target_hash, size_t max_blocks)
{
	std::deque<std::shared_ptr<nano::block>> result;

	std::deque<nano::block_hash> stack;
	stack.push_back (target_hash);
	while (!stack.empty ())
	{
		auto hash = stack.back ();
		auto block = any.block_get (transaction, hash);
		release_assert (block);

		auto dependents = dependent_blocks (transaction, *block);
		for (auto const & dependent : dependents)
		{
			if (!dependent.is_zero () && !confirmed.block_exists_or_pruned (transaction, dependent))
			{
				stats.inc (nano::stat::type::confirmation_height, nano::stat::detail::dependent_unconfirmed);

				stack.push_back (dependent);

				// Limit the stack size to avoid excessive memory usage
				// This will forget the bottom of the dependency tree
				if (stack.size () > max_blocks)
				{
					stack.pop_front ();
				}
			}
		}

		if (stack.back () == hash)
		{
			stack.pop_back ();
			if (!confirmed.block_exists_or_pruned (transaction, hash))
			{
				// We must only confirm blocks that have their dependencies confirmed
				debug_assert (dependents_confirmed (transaction, *block));
				confirm_one (transaction, *block);
				result.push_back (block);
			}
		}
		else
		{
			// Unconfirmed dependencies were added
		}

		// Refresh the transaction to avoid long-running transactions
		// Ensure that the block wasn't rolled back during the refresh
		bool refreshed = transaction.refresh_if_needed ();
		if (refreshed)
		{
			release_assert (any.block_exists (transaction, target_hash), "block was rolled back during cementing");
		}

		// Early return might leave parts of the dependency tree unconfirmed
		if (result.size () >= max_blocks)
		{
			break;
		}
	}

	return result;
}

void nano::ledger::confirm_one (secure::write_transaction & transaction, nano::block const & block)
{
	auto account_l = block.account ();
	auto hash = block.hash ();
	store.block.put (transaction, hash, block);
	unconfirmed.block.del (transaction, hash);
	++cache.block_count;
	++cache.cemented_count;
	stats.inc (nano::stat::type::confirmation_height, nano::stat::detail::blocks_confirmed);
	auto info = store.account.get (transaction, account_l);
	if (!block.previous ().is_zero ())
	{
		auto current_weight = store.rep_weight.get (transaction, info.value ().representative);
		auto new_weight = current_weight - info.value ().balance.number ();
		store.rep_weight.put (transaction, info.value ().representative, new_weight);
		if (auto existing = unconfirmed.rep_weight.get (transaction, info.value ().representative); existing.has_value () && existing.value () == new_weight)
		{
			unconfirmed.rep_weight.del (transaction, info.value ().representative);
		}
		unconfirmed.successor.del (transaction, block.previous ());
	}
	else
	{
		++cache.account_count;
	}
	auto representative = block.representative_field () ? block.representative_field ().value () : info.value ().representative;
	auto current_weight = store.rep_weight.get (transaction, representative);
	auto new_weight = current_weight + block.balance ().number ();
	store.rep_weight.put (transaction, representative, new_weight);
	if (auto existing = unconfirmed.rep_weight.get (transaction, representative); existing.has_value () && existing.value () == new_weight)
	{
		unconfirmed.rep_weight.del (transaction, representative);
	}
	if (unconfirmed.account.get (transaction, account_l).value ().head == hash)
	{
		unconfirmed.account.del (transaction, account_l);
	}
	if (block.is_send ())
	{
		auto destination_l = block.destination ();
		nano::pending_key key{ destination_l, hash };
		auto amount = any.block_amount (transaction, block.hash ()).value ();
		nano::pending_info value{ account_l, amount, block.sideband ().details.epoch };
		store.pending.put (transaction, key, value);
		unconfirmed.receivable.del (transaction, key);
		debug_assert (info.has_value ());
	}
	else if (block.is_receive ())
	{
		auto source_l = block.source ();
		nano::pending_key key{ account_l, source_l };
		store.pending.del (transaction, key);
		unconfirmed.received.del (transaction, key);
		auto amount = any.block_amount (transaction, block.hash ()).value ();
	}
	else if (!block.is_epoch ())
	{
		auto balance_l = block.balance ();
	}
	store.account.put (transaction, account_l, account_info (transaction, block, representative));
}

nano::block_status nano::ledger::process (secure::write_transaction const & transaction_a, std::shared_ptr<nano::block> block_a)
{
	debug_assert (!constants.work.validate_entry (*block_a) || constants.genesis == nano::dev::genesis);
	nano::block_check_context ctx{ *this, block_a };
	auto code = ctx.check (transaction_a);
	if (code == nano::block_status::progress)
	{
		debug_assert (block_a->has_sideband ());
		track (transaction_a, ctx.delta.value ());
	}
	return code;
}

nano::block_hash nano::ledger::representative (secure::transaction const & transaction_a, nano::block_hash const & hash_a)
{
	auto result (representative_calculated (transaction_a, hash_a));
	debug_assert (result.is_zero () || any.block_exists (transaction_a, result));
	return result;
}

nano::block_hash nano::ledger::representative_calculated (secure::transaction const & transaction_a, nano::block_hash const & hash_a)
{
	representative_visitor visitor (transaction_a, *this);
	visitor.compute (hash_a);
	return visitor.result;
}

std::string nano::ledger::block_text (char const * hash_a)
{
	return block_text (nano::block_hash (hash_a));
}

std::string nano::ledger::block_text (nano::block_hash const & hash_a)
{
	std::string result;
	auto transaction = tx_begin_read ();
	auto block_l = any.block_get (transaction, hash_a);
	if (block_l != nullptr)
	{
		block_l->serialize_json (result);
	}
	return result;
}

std::pair<nano::block_hash, nano::block_hash> nano::ledger::hash_root_random (secure::transaction const & transaction_a) const
{
	nano::block_hash hash (0);
	nano::root root (0);
	if (!pruning)
	{
		auto block (store.block.random (transaction_a));
		hash = block->hash ();
		root = block->root ();
	}
	else
	{
		uint64_t count (cache.block_count);
		auto region = nano::random_pool::generate_word64 (0, count - 1);
		// Pruned cache cannot guarantee that pruned blocks are already commited
		if (region < cache.pruned_count)
		{
			hash = store.pruned.random (transaction_a);
		}
		if (hash.is_zero ())
		{
			auto block (store.block.random (transaction_a));
			hash = block->hash ();
			root = block->root ();
		}
	}
	return std::make_pair (hash, root.as_block_hash ());
}

// Vote weight of an account
nano::uint128_t nano::ledger::weight (nano::account const & account_a) const
{
	if (check_bootstrap_weights.load ())
	{
		if (cache.block_count < bootstrap_weight_max_blocks)
		{
			auto weight = bootstrap_weights.find (account_a);
			if (weight != bootstrap_weights.end ())
			{
				return weight->second;
			}
		}
		else
		{
			check_bootstrap_weights = false;
		}
	}
	return cache.rep_weights.representation_get (account_a);
}

nano::uint128_t nano::ledger::weight_exact (secure::transaction const & txn_a, nano::account const & representative_a) const
{
	auto unconfirmed = this->unconfirmed.rep_weight.get (this->unconfirmed.tx_begin_read (), representative_a);
	if (unconfirmed.has_value ())
	{
		return unconfirmed.value ();
	}
	return store.rep_weight.get (txn_a, representative_a);
}

// Rollback blocks until `block_a' doesn't exist or it tries to penetrate the confirmation height
bool nano::ledger::rollback (secure::write_transaction const & transaction, nano::block_hash const & target, std::vector<std::shared_ptr<nano::block>> & list)
{
	if (!unconfirmed.block.exists (transaction, target))
	{
		return true;
	}
	auto error = false;
	auto block = unconfirmed.block.get (transaction, target);
	while (unconfirmed.block.exists (transaction, target))
	{
		std::deque<nano::account> queue;
		queue.push_front (block->account ());
		while (!queue.empty ())
		{
			debug_assert (!queue.empty ());
			auto account = queue.front ();
			auto account_info = unconfirmed.account.get (transaction, account).value ();
			auto hash = account_info.head;
			debug_assert (!hash.is_zero ());
			auto block = unconfirmed.block.get (transaction, hash);
			debug_assert (block != nullptr);
			std::optional<nano::account> dependency;
			if (block->is_send ())
			{
				auto destination_l = block->destination ();
				if (unconfirmed.received.exists (transaction, { destination_l, hash }))
				{
					dependency = destination_l;
				}
			}
			if (dependency.has_value ())
			{
				queue.push_front (*dependency);
			}
			else
			{
				list.push_back (block);
				unconfirmed.block.del (transaction, hash);
				if (!block->previous ().is_zero ())
				{
					unconfirmed.successor.del (transaction, block->previous ());
				}
				if (block->is_receive ())
				{
					unconfirmed.received.del (transaction, { account, block->source () });
				}
				else if (block->is_send ())
				{
					unconfirmed.receivable.del (transaction, { block->destination (), hash });
				}
				cache.rep_weights.representation_add (transaction, account_info.representative, 0 - block->balance ().number ());
				auto previous = any.block_get (transaction, block->previous ());
				if (previous != nullptr)
				{
					auto representative = any.block_get (transaction, this->representative (transaction, block->previous ()))->representative_field ().value ();
					cache.rep_weights.representation_add (transaction, representative, previous->balance ());
					unconfirmed.account.put (transaction, account, this->account_info (transaction, *previous, representative));
				}
				else
				{
					debug_assert (block->previous ().is_zero ());
					unconfirmed.account.del (transaction, account);
				}
				queue.pop_front ();
			}
		}
	}
	return error;
}

bool nano::ledger::rollback (secure::write_transaction const & transaction, nano::block_hash const & hash)
{
	std::vector<std::shared_ptr<nano::block>> rollback_list;
	return rollback (transaction, hash, rollback_list);
}

// Return latest root for account, account number if there are no blocks for this account.
nano::root nano::ledger::latest_root (secure::transaction const & transaction_a, nano::account const & account_a)
{
	auto info = any.account_get (transaction_a, account_a);
	if (!info)
	{
		return account_a;
	}
	else
	{
		return info->head;
	}
}

void nano::ledger::dump_account_chain (nano::account const & account_a, std::ostream & stream)
{
	auto transaction = tx_begin_read ();
	auto hash (any.account_head (transaction, account_a));
	while (!hash.is_zero ())
	{
		auto block_l = any.block_get (transaction, hash);
		debug_assert (block_l != nullptr);
		stream << hash.to_string () << std::endl;
		hash = block_l->previous ();
	}
}

bool nano::ledger::dependents_confirmed (secure::transaction const & transaction_a, nano::block const & block_a) const
{
	auto dependencies (dependent_blocks (transaction_a, block_a));
	return std::all_of (dependencies.begin (), dependencies.end (), [this, &transaction_a] (nano::block_hash const & hash_a) {
		auto result (hash_a.is_zero ());
		if (!result)
		{
			result = confirmed.block_exists_or_pruned (transaction_a, hash_a);
		}
		return result;
	});
}

bool nano::ledger::is_epoch_link (nano::link const & link_a) const
{
	return constants.epochs.is_epoch_link (link_a);
}

class dependent_block_visitor : public nano::block_visitor
{
public:
	dependent_block_visitor (nano::ledger const & ledger_a, nano::secure::transaction const & transaction_a) :
		ledger (ledger_a),
		transaction (transaction_a),
		result ({ 0, 0 })
	{
	}
	void send_block (nano::send_block const & block_a) override
	{
		result[0] = block_a.previous ();
	}
	void receive_block (nano::receive_block const & block_a) override
	{
		result[0] = block_a.previous ();
		result[1] = block_a.source_field ().value ();
	}
	void open_block (nano::open_block const & block_a) override
	{
		if (block_a.source_field ().value () != ledger.constants.genesis->account ())
		{
			result[0] = block_a.source_field ().value ();
		}
	}
	void change_block (nano::change_block const & block_a) override
	{
		result[0] = block_a.previous ();
	}
	void state_block (nano::state_block const & block_a) override
	{
		result[0] = block_a.hashables.previous;
		result[1] = block_a.hashables.link.as_block_hash ();
		// ledger.is_send will check the sideband first, if block_a has a loaded sideband the check that previous block exists can be skipped
		if (ledger.is_epoch_link (block_a.hashables.link) || is_send (block_a))
		{
			result[1].clear ();
		}
	}
	// This function is used in place of block->is_send () as it is tolerant to the block not having the sideband information loaded
	// This is needed for instance in vote generation on forks which have not yet had sideband information attached
	bool is_send (nano::state_block const & block) const
	{
		if (block.previous ().is_zero ())
		{
			return false;
		}
		if (block.has_sideband ())
		{
			return block.sideband ().details.is_send;
		}
		return block.balance_field ().value () < ledger.any.block_balance (transaction, block.previous ());
	}
	nano::ledger const & ledger;
	nano::secure::transaction const & transaction;
	std::array<nano::block_hash, 2> result;
};

std::array<nano::block_hash, 2> nano::ledger::dependent_blocks (secure::transaction const & transaction_a, nano::block const & block_a) const
{
	dependent_block_visitor visitor (*this, transaction_a);
	block_a.visit (visitor);
	return visitor.result;
}

/** Given the block hash of a send block, find the associated receive block that receives that send.
 *  The send block hash is not checked in any way, it is assumed to be correct.
 * @return Return the receive block on success and null on failure
 */
std::shared_ptr<nano::block> nano::ledger::find_receive_block_by_send_hash (secure::transaction const & transaction, nano::account const & destination, nano::block_hash const & send_block_hash)
{
	std::shared_ptr<nano::block> result;
	debug_assert (send_block_hash != 0);

	// get the cemented frontier
	auto head = confirmed.account_head (transaction, destination);
	auto possible_receive_block = any.block_get (transaction, head);

	// walk down the chain until the source field of a receive block matches the send block hash
	while (possible_receive_block != nullptr)
	{
		if (possible_receive_block->is_receive () && send_block_hash == possible_receive_block->source ())
		{
			// we have a match
			result = possible_receive_block;
			break;
		}

		possible_receive_block = any.block_get (transaction, possible_receive_block->previous ());
	}

	return result;
}

nano::account const & nano::ledger::epoch_signer (nano::link const & link_a) const
{
	return constants.epochs.signer (constants.epochs.epoch (link_a));
}

nano::link const & nano::ledger::epoch_link (nano::epoch epoch_a) const
{
	return constants.epochs.link (epoch_a);
}

nano::account_info nano::ledger::account_info (secure::transaction const & transaction, nano::block const & block, nano::account const & representative)
{
	nano::block_hash open = block.previous ().is_zero () ? block.hash () : any.account_get (transaction, block.account ()).value ().open_block;
	nano::account_info result{ block.hash (), representative, open, block.balance (), nano::seconds_since_epoch (), block.sideband ().height, block.sideband ().details.epoch };
	return result;
}

void nano::ledger::track (nano::secure::write_transaction const & transaction, nano::block_delta const & delta)
{
	auto & block = delta.block;
	debug_assert (block);
	debug_assert (block->has_sideband ());
	debug_assert (!unconfirmed.block.exists (transaction, block->hash ()));
	auto hash = block->hash ();
	unconfirmed.block.put (transaction, hash, *block);
	if (!block->previous ().is_zero ())
	{
		debug_assert (!unconfirmed.successor.exists (transaction, block->previous ()));
		unconfirmed.successor.put (transaction, block->previous (), hash);
	}
	auto account_l = block->account ();
	auto info = any.account_get (transaction, account_l);
	auto representative = block->representative_field () ? block->representative_field ().value () : info.value ().representative;
	unconfirmed.account.put (transaction, account_l, account_info (transaction, *block, representative));
	if (block->sideband ().details.is_send)
	{
		auto destination_l = block->destination ();
		nano::pending_key key{ destination_l, hash };
		auto amount = any.block_balance (transaction, block->previous ()).value ().number () - block->balance ().number ();
		nano::pending_info value{ account_l, amount, block->sideband ().details.epoch };
		debug_assert (!unconfirmed.receivable.exists (transaction, key));
		unconfirmed.receivable.put (transaction, key, value);
	}
	else if (block->sideband ().details.is_receive)
	{
		auto source_l = block->source ();
		nano::pending_key key{ account_l, source_l };
		debug_assert (!unconfirmed.received.exists (transaction, key));
		unconfirmed.received.put (transaction, key);
	}
	auto const & [account, amount] = delta.weight;
	if (account.has_value ())
	{
		cache.rep_weights.representation_add (transaction, account.value (), 0 - amount.value ().number ());
		
	}
	cache.rep_weights.representation_add (transaction, delta.head.representative, delta.head.balance.number ());
}

std::shared_ptr<nano::block> nano::ledger::forked_block (secure::transaction const & transaction_a, nano::block const & block_a)
{
	debug_assert (!any.block_exists (transaction_a, block_a.hash ()));
	auto root (block_a.root ());
	debug_assert (any.block_exists (transaction_a, root.as_block_hash ()) || store.account.exists (transaction_a, root.as_account ()));
	std::shared_ptr<nano::block> result;
	auto successor_l = any.block_successor (transaction_a, root.as_block_hash ());
	if (successor_l)
	{
		result = any.block_get (transaction_a, successor_l.value ());
	}
	if (result == nullptr)
	{
		auto info = any.account_get (transaction_a, root.as_account ());
		debug_assert (info);
		result = any.block_get (transaction_a, info->open_block);
		debug_assert (result != nullptr);
	}
	return result;
}

uint64_t nano::ledger::pruning_action (secure::write_transaction & transaction_a, nano::block_hash const & hash_a, uint64_t const batch_size_a)
{
	uint64_t pruned_count (0);
	nano::block_hash hash (hash_a);
	while (!hash.is_zero () && hash != constants.genesis->hash ())
	{
		auto block_l = any.block_get (transaction_a, hash);
		if (block_l != nullptr)
		{
			release_assert (confirmed.block_exists (transaction_a, hash));
			store.block.del (transaction_a, hash);
			store.pruned.put (transaction_a, hash);
			hash = block_l->previous ();
			++pruned_count;
			++cache.pruned_count;
			if (pruned_count % batch_size_a == 0)
			{
				transaction_a.commit ();
				transaction_a.renew ();
			}
		}
		else if (store.pruned.exists (transaction_a, hash))
		{
			hash = 0;
		}
		else
		{
			hash = 0;
			release_assert (false && "Error finding block for pruning");
		}
	}
	return pruned_count;
}

// A precondition is that the store is an LMDB store
bool nano::ledger::migrate_lmdb_to_rocksdb (std::filesystem::path const & data_path_a) const
{
	nano::logger logger;

	logger.info (nano::log::type::ledger, "Migrating LMDB database to RocksDB. This will take a while...");

	std::filesystem::space_info si = std::filesystem::space (data_path_a);
	auto file_size = std::filesystem::file_size (data_path_a / "data.ldb");
	const auto estimated_required_space = file_size * 0.65; // RocksDb database size is approximately 65% of the lmdb size

	if (si.available < estimated_required_space)
	{
		logger.warn (nano::log::type::ledger, "You may not have enough available disk space. Estimated free space requirement is {} GB", estimated_required_space / 1024 / 1024 / 1024);
	}

	boost::system::error_code error_chmod;
	nano::set_secure_perm_directory (data_path_a, error_chmod);
	auto rockdb_data_path = data_path_a / "rocksdb";
	std::filesystem::remove_all (rockdb_data_path);

	auto error (false);

	// Open rocksdb database
	nano::rocksdb_config rocksdb_config;
	rocksdb_config.enable = true;
	auto rocksdb_store = nano::make_store (logger, data_path_a, nano::dev::constants, false, true, rocksdb_config);

	if (!rocksdb_store->init_error ())
	{
		auto table_size = store.count (store.tx_begin_read (), tables::blocks);
		logger.info (nano::log::type::ledger, "Step 1 of 7: Converting {} entries from blocks table", table_size);
		std::atomic<std::size_t> count = 0;
		store.block.for_each_par (
		[&] (store::read_transaction const & /*unused*/, auto i, auto n) {
			auto rocksdb_transaction = rocksdb_store->tx_begin_write ();
			for (; i != n; ++i)
			{
				rocksdb_transaction.refresh_if_needed ();
				std::vector<uint8_t> vector;
				{
					nano::vectorstream stream (vector);
					nano::serialize_block (stream, *i->second.block);
					i->second.sideband.serialize (stream, i->second.block->type ());
				}
				rocksdb_store->block.raw_put (rocksdb_transaction, vector, i->first);

				if (auto count_l = ++count; count_l % 5000000 == 0)
				{
					logger.info (nano::log::type::ledger, "{} blocks converted ({}%)", count_l, count_l * 100 / table_size);
				}
			}
		});
		logger.info (nano::log::type::ledger, "{} entries converted ({}%)", count.load (), table_size > 0 ? count.load () * 100 / table_size : 100);

		table_size = store.count (store.tx_begin_read (), tables::pending);
		logger.info (nano::log::type::ledger, "Step 2 of 7: Converting {} entries from pending table", table_size);
		count = 0;
		store.pending.for_each_par (
		[&] (store::read_transaction const & /*unused*/, auto i, auto n) {
			auto rocksdb_transaction = rocksdb_store->tx_begin_write ();
			for (; i != n; ++i)
			{
				rocksdb_transaction.refresh_if_needed ();
				rocksdb_store->pending.put (rocksdb_transaction, i->first, i->second);
				if (auto count_l = ++count; count_l % 500000 == 0)
				{
					logger.info (nano::log::type::ledger, "{} entries converted ({}%)", count_l, count_l * 100 / table_size);
				}
			}
		});
		logger.info (nano::log::type::ledger, "{} entries converted ({}%)", count.load (), table_size > 0 ? count.load () * 100 / table_size : 100);

		table_size = store.count (store.tx_begin_read (), tables::confirmation_height);
		logger.info (nano::log::type::ledger, "Step 3 of 7: Converting {} entries from confirmation_height table", table_size);
		count = 0;
		store.confirmation_height.for_each_par (
		[&] (store::read_transaction const & /*unused*/, auto i, auto n) {
			auto rocksdb_transaction = rocksdb_store->tx_begin_write ();
			for (; i != n; ++i)
			{
				rocksdb_transaction.refresh_if_needed ();
				rocksdb_store->confirmation_height.put (rocksdb_transaction, i->first, i->second);
				if (auto count_l = ++count; count_l % 500000 == 0)
				{
					logger.info (nano::log::type::ledger, "{} entries converted ({}%)", count_l, count_l * 100 / table_size);
				}
			}
		});
		logger.info (nano::log::type::ledger, "{} entries converted ({}%)", count.load (), table_size > 0 ? count.load () * 100 / table_size : 100);

		table_size = store.count (store.tx_begin_read (), tables::accounts);
		logger.info (nano::log::type::ledger, "Step 4 of 7: Converting {} entries from accounts table", table_size);
		count = 0;
		store.account.for_each_par (
		[&] (store::read_transaction const & /*unused*/, auto i, auto n) {
			auto rocksdb_transaction = rocksdb_store->tx_begin_write ();
			for (; i != n; ++i)
			{
				rocksdb_transaction.refresh_if_needed ();
				rocksdb_store->account.put (rocksdb_transaction, i->first, i->second);
				if (auto count_l = ++count; count_l % 500000 == 0)
				{
					logger.info (nano::log::type::ledger, "{} entries converted ({}%)", count_l, count_l * 100 / table_size);
				}
			}
		});
		logger.info (nano::log::type::ledger, "{} entries converted ({}%)", count.load (), table_size > 0 ? count.load () * 100 / table_size : 100);

		table_size = store.count (store.tx_begin_read (), tables::rep_weights);
		logger.info (nano::log::type::ledger, "Step 5 of 7: Converting {} entries from rep_weights table", table_size);
		count = 0;
		store.rep_weight.for_each_par (
		[&] (store::read_transaction const & /*unused*/, auto i, auto n) {
			auto rocksdb_transaction = rocksdb_store->tx_begin_write ();
			for (; i != n; ++i)
			{
				rocksdb_transaction.refresh_if_needed ();
				rocksdb_store->rep_weight.put (rocksdb_transaction, i->first, i->second.number ());
				if (auto count_l = ++count; count_l % 500000 == 0)
				{
					logger.info (nano::log::type::ledger, "{} entries converted ({}%)", count_l, count_l * 100 / table_size);
				}
			}
		});
		logger.info (nano::log::type::ledger, "{} entries converted ({}%)", count.load (), table_size > 0 ? count.load () * 100 / table_size : 100);

		table_size = store.count (store.tx_begin_read (), tables::pruned);
		logger.info (nano::log::type::ledger, "Step 6 of 7: Converting {} entries from pruned table", table_size);
		count = 0;
		store.pruned.for_each_par (
		[&] (store::read_transaction const & /*unused*/, auto i, auto n) {
			auto rocksdb_transaction = rocksdb_store->tx_begin_write ();
			for (; i != n; ++i)
			{
				rocksdb_transaction.refresh_if_needed ();
				rocksdb_store->pruned.put (rocksdb_transaction, i->first);
				if (auto count_l = ++count; count_l % 500000 == 0)
				{
					logger.info (nano::log::type::ledger, "{} entries converted ({}%)", count_l, count_l * 100 / table_size);
				}
			}
		});
		logger.info (nano::log::type::ledger, "{} entries converted ({}%)", count.load (), table_size > 0 ? count.load () * 100 / table_size : 100);

		table_size = store.count (store.tx_begin_read (), tables::final_votes);
		logger.info (nano::log::type::ledger, "Step 7 of 7: Converting {} entries from final_votes table", table_size);
		count = 0;
		store.final_vote.for_each_par (
		[&] (store::read_transaction const & /*unused*/, auto i, auto n) {
			auto rocksdb_transaction = rocksdb_store->tx_begin_write ();
			for (; i != n; ++i)
			{
				rocksdb_transaction.refresh_if_needed ();
				rocksdb_store->final_vote.put (rocksdb_transaction, i->first, i->second);
				if (auto count_l = ++count; count_l % 500000 == 0)
				{
					logger.info (nano::log::type::ledger, "{} entries converted ({}%)", count_l, count_l * 100 / table_size);
				}
			}
		});
		logger.info (nano::log::type::ledger, "{} entries converted ({}%)", count.load (), table_size > 0 ? count.load () * 100 / table_size : 100);

		logger.info (nano::log::type::ledger, "Finalizing migration...");
		auto lmdb_transaction (store.tx_begin_read ());
		auto version = store.version.get (lmdb_transaction);
		auto rocksdb_transaction (rocksdb_store->tx_begin_write ());
		rocksdb_store->version.put (rocksdb_transaction, version);

		for (auto i (store.online_weight.begin (lmdb_transaction)), n (store.online_weight.end ()); i != n; ++i)
		{
			rocksdb_store->online_weight.put (rocksdb_transaction, i->first, i->second);
		}

		for (auto i (store.peer.begin (lmdb_transaction)), n (store.peer.end ()); i != n; ++i)
		{
			rocksdb_store->peer.put (rocksdb_transaction, i->first, i->second);
		}

		// Compare counts
		error |= store.peer.count (lmdb_transaction) != rocksdb_store->peer.count (rocksdb_transaction);
		error |= store.pruned.count (lmdb_transaction) != rocksdb_store->pruned.count (rocksdb_transaction);
		error |= store.final_vote.count (lmdb_transaction) != rocksdb_store->final_vote.count (rocksdb_transaction);
		error |= store.online_weight.count (lmdb_transaction) != rocksdb_store->online_weight.count (rocksdb_transaction);
		error |= store.rep_weight.count (lmdb_transaction) != rocksdb_store->rep_weight.count (rocksdb_transaction);
		error |= store.version.get (lmdb_transaction) != rocksdb_store->version.get (rocksdb_transaction);

		// For large tables a random key is used instead and makes sure it exists
		auto random_block (store.block.random (lmdb_transaction));
		error |= rocksdb_store->block.get (rocksdb_transaction, random_block->hash ()) == nullptr;

		auto account = random_block->account ();
		nano::account_info account_info;
		error |= rocksdb_store->account.get (rocksdb_transaction, account, account_info);

		// If confirmation height exists in the lmdb ledger for this account it should exist in the rocksdb ledger
		nano::confirmation_height_info confirmation_height_info{};
		if (!store.confirmation_height.get (lmdb_transaction, account, confirmation_height_info))
		{
			error |= rocksdb_store->confirmation_height.get (rocksdb_transaction, account, confirmation_height_info);
		}

		logger.info (nano::log::type::ledger, "Migration completed. Make sure to enable RocksDb in the config file under [node.rocksdb]");
		logger.info (nano::log::type::ledger, "After confirming correct node operation, the data.ldb file can be deleted if no longer required");
	}
	else
	{
		error = true;
	}
	return error;
}

bool nano::ledger::bootstrap_weight_reached () const
{
	return cache.block_count >= bootstrap_weight_max_blocks;
}

nano::epoch nano::ledger::version (nano::block const & block)
{
	if (block.type () == nano::block_type::state)
	{
		return block.sideband ().details.epoch;
	}

	return nano::epoch::epoch_0;
}

nano::epoch nano::ledger::version (secure::transaction const & transaction, nano::block_hash const & hash) const
{
	auto block_l = any.block_get (transaction, hash);
	if (block_l == nullptr)
	{
		return nano::epoch::epoch_0;
	}
	return version (*block_l);
}

uint64_t nano::ledger::cemented_count () const
{
	return cache.cemented_count;
}

uint64_t nano::ledger::block_count () const
{
	return cache.block_count;
}

uint64_t nano::ledger::account_count () const
{
	return cache.account_count;
}

uint64_t nano::ledger::pruned_count () const
{
	return cache.pruned_count;
}

std::unique_ptr<nano::container_info_component> nano::ledger::collect_container_info (std::string const & name) const
{
	auto count = bootstrap_weights.size ();
	auto sizeof_element = sizeof (decltype (bootstrap_weights)::value_type);
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "bootstrap_weights", count, sizeof_element }));
	composite->add_component (cache.rep_weights.collect_container_info ("rep_weights"));
	return composite;
}
