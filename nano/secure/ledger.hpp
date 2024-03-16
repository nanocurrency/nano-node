#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/timer.hpp>
#include <nano/secure/account_info.hpp>
#include <nano/secure/generate_cache_flags.hpp>
#include <nano/secure/ledger_cache.hpp>
#include <nano/secure/pending_info.hpp>

#include <map>

namespace nano::store
{
class component;
class transaction;
class write_transaction;
}

namespace nano
{
class block;
enum class block_status;
enum class epoch : uint8_t;
class ledger_constants;
class pending_info;
class pending_key;
class stats;

class uncemented_info
{
public:
	uncemented_info (nano::block_hash const & cemented_frontier, nano::block_hash const & frontier, nano::account const & account);
	nano::block_hash cemented_frontier;
	nano::block_hash frontier;
	nano::account account;
};

class ledger final
{
	friend class receivable_iterator;

public:
	ledger (nano::store::component &, nano::stats &, nano::ledger_constants & constants, nano::generate_cache_flags const & = nano::generate_cache_flags{});
	/**
	 * Returns the account for a given hash
	 * Returns std::nullopt if the block doesn't exist or has been pruned
	 */
	std::optional<nano::account> account (store::transaction const &, nano::block_hash const &) const;
	std::optional<nano::account_info> account_info (store::transaction const & transaction, nano::account const & account) const;
	std::optional<nano::uint128_t> amount (store::transaction const &, nano::block_hash const &);
	std::optional<nano::uint128_t> balance (store::transaction const &, nano::block_hash const &) const;
	std::shared_ptr<nano::block> block (store::transaction const & transaction, nano::block_hash const & hash) const;
	bool block_exists (store::transaction const & transaction, nano::block_hash const & hash) const;
	nano::uint128_t account_balance (store::transaction const &, nano::account const &, bool = false);
	nano::uint128_t account_receivable (store::transaction const &, nano::account const &, bool = false);
	nano::uint128_t weight (nano::account const &);
	std::optional<nano::block_hash> successor (store::transaction const &, nano::qualified_root const &) const noexcept;
	std::optional<nano::block_hash> successor (store::transaction const & transaction, nano::block_hash const & hash) const noexcept;
	std::shared_ptr<nano::block> forked_block (store::transaction const &, nano::block const &);
	std::shared_ptr<nano::block> head_block (store::transaction const &, nano::account const &);
	bool block_confirmed (store::transaction const &, nano::block_hash const &) const;
	nano::block_hash latest (store::transaction const &, nano::account const &);
	nano::root latest_root (store::transaction const &, nano::account const &);
	nano::block_hash representative (store::transaction const &, nano::block_hash const &);
	nano::block_hash representative_calculated (store::transaction const &, nano::block_hash const &);
	bool block_or_pruned_exists (nano::block_hash const &) const;
	bool block_or_pruned_exists (store::transaction const &, nano::block_hash const &) const;
	std::string block_text (char const *);
	std::string block_text (nano::block_hash const &);
	std::pair<nano::block_hash, nano::block_hash> hash_root_random (store::transaction const &) const;
	std::optional<nano::pending_info> pending_info (store::transaction const & transaction, nano::pending_key const & key) const;
	nano::block_status process (store::write_transaction const & transaction, std::shared_ptr<nano::block> block);
	bool rollback (store::write_transaction const &, nano::block_hash const &, std::vector<std::shared_ptr<nano::block>> &);
	bool rollback (store::write_transaction const &, nano::block_hash const &);
	void update_account (store::write_transaction const &, nano::account const &, nano::account_info const &, nano::account_info const &);
	uint64_t pruning_action (store::write_transaction &, nano::block_hash const &, uint64_t const);
	void dump_account_chain (nano::account const &, std::ostream & = std::cout);
	bool dependents_confirmed (store::transaction const &, nano::block const &) const;
	bool is_epoch_link (nano::link const &) const;
	std::array<nano::block_hash, 2> dependent_blocks (store::transaction const &, nano::block const &) const;
	std::shared_ptr<nano::block> find_receive_block_by_send_hash (store::transaction const & transaction, nano::account const & destination, nano::block_hash const & send_block_hash);
	nano::account const & epoch_signer (nano::link const &) const;
	nano::link const & epoch_link (nano::epoch) const;
	std::multimap<uint64_t, uncemented_info, std::greater<>> unconfirmed_frontiers () const;
	bool migrate_lmdb_to_rocksdb (std::filesystem::path const &) const;
	bool bootstrap_weight_reached () const;
	static nano::epoch version (nano::block const & block);
	nano::epoch version (store::transaction const & transaction, nano::block_hash const & hash) const;
	uint64_t height (store::transaction const & transaction, nano::block_hash const & hash) const;
	// Returns whether there are any receivable entries for 'account'
	bool receivable_any (store::transaction const & tx, nano::account const & account) const;
	nano::receivable_iterator receivable_end () const;
	// Returns the next receivable entry for an account greater than 'account'
	nano::receivable_iterator receivable_upper_bound (store::transaction const & tx, nano::account const & account) const;
	// Returns the next receivable entry for the account 'account' with hash greater than 'hash'
	nano::receivable_iterator receivable_upper_bound (store::transaction const & tx, nano::account const & account, nano::block_hash const & hash) const;
	static nano::uint128_t const unit;
	nano::ledger_constants & constants;
	nano::store::component & store;
	nano::ledger_cache cache;
	nano::stats & stats;
	std::unordered_map<nano::account, nano::uint128_t> bootstrap_weights;
	uint64_t bootstrap_weight_max_blocks{ 1 };
	std::atomic<bool> check_bootstrap_weights;
	bool pruning{ false };

private:
	// Returns the next receivable entry equal or greater than 'key'
	std::optional<std::pair<nano::pending_key, nano::pending_info>> receivable_lower_bound (store::transaction const & tx, nano::account const & account, nano::block_hash const & hash) const;
	void initialize (nano::generate_cache_flags const &);
};

std::unique_ptr<container_info_component> collect_container_info (ledger & ledger, std::string const & name);
}
