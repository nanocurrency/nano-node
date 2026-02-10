#include <nano/crypto_lib/random_pool.hpp>
#include <nano/lib/block_type.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/files.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/utility.hpp>
#include <nano/lib/work.hpp>
#include <nano/node/make_store.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_processor.hpp>
#include <nano/secure/ledger_rollback.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/secure/ledger_set_confirmed.hpp>
#include <nano/secure/rep_weights.hpp>
#include <nano/store/ledger/account.hpp>
#include <nano/store/ledger/block.hpp>
#include <nano/store/ledger/confirmation_height.hpp>
#include <nano/store/ledger/final_vote.hpp>
#include <nano/store/ledger/online_weight.hpp>
#include <nano/store/ledger/peer.hpp>
#include <nano/store/ledger/pending.hpp>
#include <nano/store/ledger/pruned.hpp>
#include <nano/store/ledger/rep_weight.hpp>
#include <nano/store/ledger/version.hpp>
#include <nano/store/ledger_store.hpp>

#include <stack>

#include <cryptopp/words.h>

nano::ledger::ledger (nano::store::ledger_store & store_a, nano::network_params const & params_a, nano::stats & stats_a, nano::logger & logger_a, nano::generate_cache_flags generate_cache_flags_a, nano::uint128_t min_rep_weight_a, uint64_t max_backlog_a) :
	constants{ params_a.ledger },
	work{ params_a.work },
	store{ store_a },
	stats{ stats_a },
	logger{ logger_a },
	rep_weights{ store_a.rep_weight, min_rep_weight_a },
	max_backlog_size{ max_backlog_a },
	any_impl{ std::make_unique<ledger_set_any> (*this) },
	confirmed_impl{ std::make_unique<ledger_set_confirmed> (*this) },
	any{ *any_impl },
	confirmed{ *confirmed_impl }
{
	initialize (generate_cache_flags_a);
}

nano::ledger::~ledger ()
{
}

auto nano::ledger::tx_begin_write (nano::store::writer guard_type) const -> secure::write_transaction
{
	auto guard = store.write_queue.wait (guard_type);
	auto txn = store.tx_begin_write ();
	return secure::write_transaction{ std::move (txn), std::move (guard) };
}

auto nano::ledger::tx_begin_read () const -> secure::read_transaction
{
	return secure::read_transaction{ store.tx_begin_read () };
}

void nano::ledger::initialize (nano::generate_cache_flags const & generate_cache_flags)
{
	debug_assert (rep_weights.empty ());

	logger.info (nano::log::type::ledger, "Loading ledger, this may take a while...");

	bool is_initialized = false;
	{
		auto const transaction = store.tx_begin_read ();
		is_initialized = (store.account.begin (transaction) != store.account.end (transaction));
	}
	if (!is_initialized && store.get_mode () != nano::store::open_mode::read_only)
	{
		// Store was empty meaning we just created it, add the genesis block
		logger.info (nano::log::type::ledger, "Initializing ledger with genesis block: {}", constants.genesis->hash ());
		auto const transaction = store.tx_begin_write ();
		store.initialize (transaction, constants);
	}

	if (generate_cache_flags.account_count || generate_cache_flags.block_count)
	{
		logger.debug (nano::log::type::ledger, "Generating block count cache...");

		store.account.for_each_par (
		[this] (store::read_transaction const &, auto i, auto n) {
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

		logger.debug (nano::log::type::ledger, "Block count cache generated");
	}

	if (generate_cache_flags.cemented_count)
	{
		logger.debug (nano::log::type::ledger, "Generating cemented count cache...");

		store.confirmation_height.for_each_par (
		[this] (store::read_transaction const &, auto i, auto n) {
			uint64_t cemented_count_l (0);
			for (; i != n; ++i)
			{
				cemented_count_l += i->second.height;
			}
			this->cache.cemented_count += cemented_count_l;
		});

		logger.debug (nano::log::type::ledger, "Cemented count cache generated");
	}

	{
		logger.debug (nano::log::type::ledger, "Generating pruned count cache...");

		auto transaction = store.tx_begin_read ();
		cache.pruned_count = store.pruned.count (transaction);

		logger.debug (nano::log::type::ledger, "Pruned count cache generated");
	}

	if (generate_cache_flags.reps)
	{
		logger.debug (nano::log::type::ledger, "Generating representative weights cache...");

		store.rep_weight.for_each_par (
		[this] (store::read_transaction const &, auto i, auto n) {
			nano::rep_weights rep_weights_l{ this->store.rep_weight };
			for (; i != n; ++i)
			{
				rep_weights_l.put (i->first, i->second.number ());
			}
			this->rep_weights.append_from (rep_weights_l);
		});

		store.pending.for_each_par (
		[this] (store::read_transaction const &, auto i, auto n) {
			nano::rep_weights rep_weights_l{ this->store.rep_weight };
			for (; i != n; ++i)
			{
				rep_weights_l.put_unused (i->second.amount.number ());
			}
			this->rep_weights.append_from (rep_weights_l);
		});

		logger.debug (nano::log::type::ledger, "Representative weights cache generated");
	}

	// Use larger precision types to detect potential overflow issues
	nano::uint256_t active_balance, pending_balance, burned_balance;

	if (generate_cache_flags.consistency_check)
	{
		logger.debug (nano::log::type::ledger, "Verifying ledger balance consistency...");

		// Verify sum of all account and pending balances
		nano::locked<nano::uint256_t> active_balance_s{ 0 };
		nano::locked<nano::uint256_t> pending_balance_s{ 0 };
		nano::locked<nano::uint256_t> burned_balance_s{ 0 };

		store.account.for_each_par (
		[&] (store::read_transaction const &, auto i, auto n) {
			nano::uint256_t balance_l{ 0 };
			nano::uint256_t burned_l{ 0 };
			for (; i != n; ++i)
			{
				nano::account_info const & info = i->second;
				if (i->first == constants.burn_account)
				{
					burned_l += info.balance.number ();
				}
				else
				{
					balance_l += info.balance.number ();
				}
			}
			(*active_balance_s.lock ()) += balance_l;
			release_assert (burned_l == 0); // The burn account should not have any active balance
		});

		store.pending.for_each_par (
		[&] (store::read_transaction const &, auto i, auto n) {
			nano::uint256_t balance_l{ 0 };
			nano::uint256_t burned_l{ 0 };
			for (; i != n; ++i)
			{
				nano::pending_key const & key = i->first;
				nano::pending_info const & info = i->second;
				if (key.account == constants.burn_account)
				{
					burned_l += info.amount.number ();
				}
				else
				{
					balance_l += info.amount.number ();
				}
			}
			(*pending_balance_s.lock ()) += balance_l;
			(*burned_balance_s.lock ()) += burned_l;
		});

		active_balance = *active_balance_s.lock ();
		pending_balance = *pending_balance_s.lock ();
		burned_balance = *burned_balance_s.lock ();

		release_assert (active_balance <= std::numeric_limits<nano::uint128_t>::max ());
		release_assert (pending_balance <= std::numeric_limits<nano::uint128_t>::max ());
		release_assert (burned_balance <= std::numeric_limits<nano::uint128_t>::max ());

		release_assert (active_balance + pending_balance + burned_balance == constants.genesis_amount, "ledger corruption detected: account and pending balances do not match genesis amount", to_string (active_balance) + " + " + to_string (pending_balance) + " + " + to_string (burned_balance) + " != " + to_string (constants.genesis_amount));
		release_assert (active_balance == rep_weights.get_weight_committed (), "ledger corruption detected: active balance does not match committed representative weights", to_string (active_balance) + " != " + to_string (rep_weights.get_weight_committed ()));
		release_assert (pending_balance + burned_balance == rep_weights.get_weight_unused (), "ledger corruption detected: pending balance does not match unused representative weights", to_string (pending_balance) + " != " + to_string (rep_weights.get_weight_unused ()));

		logger.debug (nano::log::type::ledger, "Ledger balance consistency verified");
	}
	else
	{
		logger.warn (nano::log::type::ledger, "Ledger consistency check skipped; ensure your environment provides data-integrity safeguards");
	}

	if (generate_cache_flags.reps && generate_cache_flags.consistency_check)
	{
		logger.debug (nano::log::type::ledger, "Verifying total weights consistency...");

		rep_weights.verify_consistency (static_cast<nano::uint128_t> (burned_balance));

		logger.debug (nano::log::type::ledger, "Total weights consistency verified");
	}

	logger.info (nano::log::type::ledger, "Block count:    {:>11}", cache.block_count.load ());
	logger.info (nano::log::type::ledger, "Cemented count: {:>11}", cache.cemented_count.load ());
	logger.info (nano::log::type::ledger, "Account count:  {:>11}", cache.account_count.load ());
	logger.info (nano::log::type::ledger, "Pruned count:   {:>11}", cache.pruned_count.load ());
	logger.info (nano::log::type::ledger, "Representative count: {:>5}", rep_weights.size ());
	logger.info (nano::log::type::ledger, "Active balance: {} | pending: {} | burned: {}",
	nano::uint128_union{ static_cast<nano::uint128_t> (active_balance) }.format_balance (nano::nano_ratio, 0, true),
	nano::uint128_union{ static_cast<nano::uint128_t> (pending_balance) }.format_balance (nano::nano_ratio, 0, true),
	nano::uint128_union{ static_cast<nano::uint128_t> (burned_balance) }.format_balance (nano::nano_ratio, 0, true));
	logger.info (nano::log::type::ledger, "Weight committed: {} | unused: {}",
	nano::uint128_union{ rep_weights.get_weight_committed () }.format_balance (nano::nano_ratio, 0, true),
	nano::uint128_union{ rep_weights.get_weight_unused () }.format_balance (nano::nano_ratio, 0, true));
}

void nano::ledger::verify_consistency (secure::transaction const & transaction) const
{
	rep_weights.verify_consistency (0); // It's impractical to recompute burned weight, so we skip it here
}

bool nano::ledger::unconfirmed_exists (secure::transaction const & transaction, nano::block_hash const & hash) const
{
	return any.block_exists (transaction, hash) && !confirmed.block_exists (transaction, hash);
}

nano::uint128_t nano::ledger::account_receivable (secure::transaction const & transaction_a, nano::account const & account_a, bool only_confirmed_a) const
{
	nano::uint128_t result (0);
	nano::account end (account_a.number () + 1);
	for (auto i (store.pending.begin (transaction_a, nano::pending_key (account_a, 0))), n (store.pending.begin (transaction_a, nano::pending_key (end, 0))); i != n; ++i)
	{
		nano::pending_info const & info (i->second);
		if (only_confirmed_a)
		{
			if (confirmed.block_exists_or_pruned (transaction_a, i->first.hash))
			{
				result += info.amount.number ();
			}
		}
		else
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

		auto dependencies = block_dependencies (transaction, *block);
		for (auto const & dependency : dependencies)
		{
			if (!dependency.is_zero () && !confirmed.block_exists_or_pruned (transaction, dependency))
			{
				stats.inc (nano::stat::type::confirmation_height, nano::stat::detail::dependency_unconfirmed);

				stack.push_back (dependency);

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
				debug_assert (dependencies_confirmed (transaction, *block));
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
			if (!any.block_exists (transaction, target_hash))
			{
				break; // Block was rolled back during cementing
			}
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
	debug_assert ((!store.confirmation_height.get (transaction, block.account ()) && block.sideband ().height == 1) || store.confirmation_height.get (transaction, block.account ()).value ().height + 1 == block.sideband ().height);
	confirmation_height_info info{ block.sideband ().height, block.hash () };
	store.confirmation_height.put (transaction, block.account (), info);
	++cache.cemented_count;

	stats.inc (nano::stat::type::confirmation_height, nano::stat::detail::blocks_confirmed);
}

nano::block_status nano::ledger::process (secure::write_transaction const & transaction, std::shared_ptr<nano::block> block)
{
	debug_assert (!work.validate_entry (*block) || constants.genesis == nano::dev::genesis);

	ledger_processor processor (transaction, *this);
	block->visit (processor);
	if (processor.result == nano::block_status::progress)
	{
		++cache.block_count;
	}
	return processor.result;
}

namespace
{
class representative_block_visitor final : public nano::block_visitor
{
public:
	representative_block_visitor (nano::secure::transaction const & transaction, nano::ledger & ledger) :
		transaction{ transaction },
		ledger{ ledger }
	{
	}

	void compute (nano::block_hash const & hash)
	{
		current = hash;
		while (result.is_zero ())
		{
			auto block = ledger.any.block_get (transaction, current);
			release_assert (block != nullptr);
			block->visit (*this);
		}
	}

	void send_block (nano::send_block const & block) override
	{
		current = block.previous ();
	}
	void receive_block (nano::receive_block const & block) override
	{
		current = block.previous ();
	}
	void open_block (nano::open_block const & block) override
	{
		result = block.hash ();
	}
	void change_block (nano::change_block const & block) override
	{
		result = block.hash ();
	}
	void state_block (nano::state_block const & block) override
	{
		result = block.hash ();
	}

	nano::secure::transaction const & transaction;
	nano::ledger & ledger;

	nano::block_hash current{ 0 };
	nano::block_hash result{ 0 };
};
}

nano::block_hash nano::ledger::representative_block (secure::transaction const & transaction, nano::block_hash const & hash)
{
	representative_block_visitor visitor{ transaction, *this };
	visitor.compute (hash);
	auto result = visitor.result;
	debug_assert (result.is_zero () || any.block_exists (transaction, result));
	return result;
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

std::deque<std::shared_ptr<nano::block>> nano::ledger::random_blocks (secure::transaction const & transaction, size_t count) const
{
	std::deque<std::shared_ptr<nano::block>> result;

	auto const starting_hash = nano::random_pool::generate<nano::block_hash> ();

	// It is more efficient to choose a random starting point and pick a few sequential blocks from there
	auto it = store.block.begin (transaction, starting_hash);
	auto const end = store.block.end (transaction);
	while (result.size () < count)
	{
		if (it != end)
		{
			result.push_back (it->second.block);
		}
		++it; // Store iterators wrap around when reaching the end
	}

	return result;
}

bool nano::ledger::bootstrap_height_reached () const
{
	return cache.block_count >= bootstrap_weights.max_blocks;
}

std::unordered_map<nano::account, nano::uint128_t> nano::ledger::rep_weights_snapshot () const
{
	if (!bootstrap_height_reached ())
	{
		return bootstrap_weights.representatives;
	}
	else
	{
		return rep_weights.get_rep_amounts ();
	}
}

nano::uint128_t nano::ledger::weight (nano::account const & account) const
{
	if (!bootstrap_height_reached ())
	{
		auto weight = bootstrap_weights.representatives.find (account);
		if (weight != bootstrap_weights.representatives.end ())
		{
			return weight->second;
		}
		return 0;
	}
	else
	{
		return rep_weights.get (account);
	}
}

nano::uint128_t nano::ledger::weight_exact (secure::transaction const & txn_a, nano::account const & representative_a) const
{
	return store.rep_weight.get (txn_a, representative_a);
}

// Rollback blocks until `block_a' doesn't exist or it tries to penetrate the confirmation height
// TODO: Refactor rollback operation to use non-recursive algorithm
bool nano::ledger::rollback (secure::write_transaction const & transaction_a, nano::block_hash const & block_a, std::deque<std::shared_ptr<nano::block>> & list_a, size_t depth, size_t const max_depth)
{
	if (depth > max_depth)
	{
		logger.critical (nano::log::type::ledger, "Rollback depth exceeded: {} (max depth: {})", depth, max_depth);
		return true; // Error
	}

	debug_assert (any.block_exists (transaction_a, block_a));
	auto account_l = any.block_account (transaction_a, block_a).value ();
	auto block_account_height (any.block_height (transaction_a, block_a));
	ledger_rollback rollback (transaction_a, *this, list_a, depth, max_depth);
	auto error (false);
	while (!error && any.block_exists (transaction_a, block_a))
	{
		nano::confirmation_height_info confirmation_height_info;
		store.confirmation_height.get (transaction_a, account_l, confirmation_height_info);
		if (block_account_height > confirmation_height_info.height)
		{
			auto info = any.account_get (transaction_a, account_l);
			release_assert (info);
			auto block_l = any.block_get (transaction_a, info->head);
			release_assert (block_l != nullptr);
			block_l->visit (rollback);
			error = rollback.error;
			if (!error)
			{
				list_a.push_back (block_l);
				--cache.block_count;
			}
		}
		else
		{
			error = true;
		}
	}
	return error;
}

bool nano::ledger::rollback (secure::write_transaction const & transaction_a, nano::block_hash const & block_a)
{
	std::deque<std::shared_ptr<nano::block>> rollback_list;
	return rollback (transaction_a, block_a, rollback_list);
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

bool nano::ledger::dependencies_confirmed (secure::transaction const & transaction_a, nano::block const & block_a) const
{
	auto dependencies (block_dependencies (transaction_a, block_a));
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

namespace
{
class dependency_block_visitor final : public nano::block_visitor
{
public:
	dependency_block_visitor (nano::secure::transaction const & transaction, nano::ledger const & ledger) :
		ledger{ ledger },
		transaction{ transaction }
	{
	}

	void send_block (nano::send_block const & block) override
	{
		result[0] = block.previous ();
	}
	void receive_block (nano::receive_block const & block) override
	{
		result[0] = block.previous ();
		result[1] = block.source_field ().value ();
	}
	void open_block (nano::open_block const & block) override
	{
		if (block.source_field ().value () != ledger.constants.genesis->account ().as_union ())
		{
			result[0] = block.source_field ().value ();
		}
	}
	void change_block (nano::change_block const & block) override
	{
		result[0] = block.previous ();
	}
	void state_block (nano::state_block const & block) override
	{
		result[0] = block.hashables.previous;
		result[1] = block.hashables.link.as_block_hash ();
		// ledger.is_send will check the sideband first, if block_a has a loaded sideband the check that previous block exists can be skipped
		if (ledger.is_epoch_link (block.hashables.link) || is_send (block))
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

	std::array<nano::block_hash, 2> result{ 0, 0 };
};
}

std::array<nano::block_hash, 2> nano::ledger::block_dependencies (secure::transaction const & transaction, nano::block const & block) const
{
	dependency_block_visitor visitor{ transaction, *this };
	block.visit (visitor);
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
	nano::confirmation_height_info info;
	if (store.confirmation_height.get (transaction, destination, info))
	{
		return nullptr;
	}
	auto possible_receive_block = any.block_get (transaction, info.frontier);

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

std::optional<nano::account> nano::ledger::linked_account (secure::transaction const & transaction, nano::block const & block)
{
	if (block.is_send ())
	{
		return block.destination ();
	}
	else if (block.is_receive ())
	{
		return any.block_account (transaction, block.source ());
	}
	return std::nullopt;
}

nano::account const & nano::ledger::epoch_signer (nano::link const & link_a) const
{
	return constants.epochs.signer (constants.epochs.epoch (link_a));
}

nano::link const & nano::ledger::epoch_link (nano::epoch epoch_a) const
{
	return constants.epochs.link (epoch_a);
}

void nano::ledger::update_account (secure::write_transaction const & transaction_a, nano::account const & account_a, nano::account_info const & old_a, nano::account_info const & new_a)
{
	if (!new_a.head.is_zero ())
	{
		if (old_a.head.is_zero () && new_a.open_block == new_a.head)
		{
			++cache.account_count;
		}
		if (!old_a.head.is_zero () && old_a.epoch () != new_a.epoch ())
		{
			// store.account.put won't erase existing entries if they're in different tables
			store.account.del (transaction_a, account_a);
		}
		store.account.put (transaction_a, account_a, new_a);
	}
	else
	{
		debug_assert (!store.confirmation_height.exists (transaction_a, account_a));
		store.account.del (transaction_a, account_a);
		release_assert (cache.account_count > 0);
		--cache.account_count;
	}
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
		release_assert (info);
		result = any.block_get (transaction_a, info->open_block);
		release_assert (result != nullptr);
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
			release_assert (false, "error finding block for pruning");
		}
	}
	return pruned_count;
}

// Balance uses the maximum of current and previous block balance to avoid deprioritizing full sends
// Timestamp uses the previous block's timestamp for least-recently-used ordering within a bucket,
// falling back to the current block's sideband timestamp when there is no previous block (e.g. open blocks)
auto nano::ledger::block_priority (nano::secure::transaction const & transaction, nano::block const & block) const -> block_priority_result
{
	auto const balance = block.balance ();
	auto const previous_block = !block.previous ().is_zero () ? any.block_get (transaction, block.previous ()) : nullptr;
	auto const previous_balance = previous_block ? previous_block->balance () : 0;

	// Handle full send case nicely where the balance would otherwise be 0
	auto const priority_balance = std::max (balance, block.is_send () ? previous_balance : 0);

	// Use previous block timestamp as priority timestamp for least recently used prioritization within the same bucket
	// Account info timestamp is not used here because it will get out of sync when rollbacks happen
	auto const priority_timestamp = previous_block ? previous_block->sideband ().timestamp : block.sideband ().timestamp;
	return { priority_balance, priority_timestamp };
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

uint64_t nano::ledger::backlog_size () const
{
	auto blocks = cache.block_count.load ();
	auto cemented = cache.cemented_count.load ();
	return (blocks > cemented) ? blocks - cemented : 0;
}

uint64_t nano::ledger::max_backlog () const
{
	auto const count = cemented_count ();
	auto const max_bootstrap_count = bootstrap_weights.max_blocks;

	if (max_backlog_size == 0)
	{
		return 0; // Unlimited backlog
	}

	// Use cemented block count to determine the switch point for backlog
	if (count >= max_bootstrap_count)
	{
		return max_backlog_size;
	}
	else
	{
		// If the bootstrap weight hasn't been reached, we allow a backlog of up to bootstrap_weights.max_blocks
		// This should avoid having to rollback too many blocks once the bootstrap weight is reached
		auto const allowed_backlog = max_bootstrap_count - count;
		return std::max (allowed_backlog, max_backlog_size);
	}
}

nano::container_info nano::ledger::container_info () const
{
	nano::container_info info;
	info.put ("bootstrap_weights", bootstrap_weights.representatives);
	info.add ("rep_weights", rep_weights.container_info ());
	return info;
}
