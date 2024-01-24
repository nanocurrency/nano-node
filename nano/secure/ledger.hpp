#pragma once

#include <nano/lib/rep_weights.hpp>
#include <nano/lib/timer.hpp>
#include <nano/secure/common.hpp>

#include <map>

namespace nano::store
{
class component;
class transaction;
class write_transaction;
}

namespace nano
{
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
public:
	ledger (nano::store::component &, nano::stats &, nano::ledger_constants & constants, nano::generate_cache const & = nano::generate_cache ());
	/**
	 * Return account containing hash, expects that block hash exists in ledger
	 */
	nano::account account (nano::block const & block) const;
	nano::account account (store::transaction const &, nano::block_hash const &) const;
	std::optional<nano::account_info> account_info (store::transaction const & transaction, nano::account const & account) const;
	/**
	 * For non-prunning nodes same as `ledger::account()`
	 * For prunning nodes ensures that block hash exists, otherwise returns zero account
	 */
	nano::account account_safe (store::transaction const &, nano::block_hash const &, bool &) const;
	/**
	 * Return account containing hash, returns zero account if account can not be found
	 */
	nano::account account_safe (store::transaction const &, nano::block_hash const &) const;
	nano::uint128_t amount (store::transaction const &, nano::account const &);
	nano::uint128_t amount (store::transaction const &, nano::block_hash const &);
	/** Safe for previous block, but block hash_a must exist */
	nano::uint128_t amount_safe (store::transaction const &, nano::block_hash const & hash_a, bool &) const;
	static nano::uint128_t balance (nano::block const & block);
	nano::uint128_t balance (store::transaction const &, nano::block_hash const &) const;
	nano::uint128_t balance_safe (store::transaction const &, nano::block_hash const &, bool &) const;
	nano::uint128_t account_balance (store::transaction const &, nano::account const &, bool = false);
	nano::uint128_t account_receivable (store::transaction const &, nano::account const &, bool = false);
	nano::uint128_t weight (nano::account const &);
	std::shared_ptr<nano::block> successor (store::transaction const &, nano::qualified_root const &);
	std::shared_ptr<nano::block> forked_block (store::transaction const &, nano::block const &);
	std::shared_ptr<nano::block> head_block (store::transaction const &, nano::account const &);
	bool block_confirmed (store::transaction const &, nano::block_hash const &) const;
	nano::block_hash latest (store::transaction const &, nano::account const &);
	nano::root latest_root (store::transaction const &, nano::account const &);
	nano::block_hash representative (store::transaction const &, nano::block_hash const &);
	nano::block_hash representative_calculated (store::transaction const &, nano::block_hash const &);
	bool block_or_pruned_exists (nano::block_hash const &) const;
	bool block_or_pruned_exists (store::transaction const &, nano::block_hash const &) const;
	bool root_exists (store::transaction const &, nano::root const &);
	std::string block_text (char const *);
	std::string block_text (nano::block_hash const &);
	bool is_send (store::transaction const &, nano::block const &) const;
	nano::account const & block_destination (store::transaction const &, nano::block const &);
	nano::block_hash block_source (store::transaction const &, nano::block const &);
	std::pair<nano::block_hash, nano::block_hash> hash_root_random (store::transaction const &) const;
	std::optional<nano::pending_info> pending_info (store::transaction const & transaction, nano::pending_key const & key) const;
	nano::process_return process (store::write_transaction const &, nano::block &);
	bool rollback (store::write_transaction const &, nano::block_hash const &, std::vector<std::shared_ptr<nano::block>> &);
	bool rollback (store::write_transaction const &, nano::block_hash const &);
	void update_account (store::write_transaction const &, nano::account const &, nano::account_info const &, nano::account_info const &);
	uint64_t pruning_action (store::write_transaction &, nano::block_hash const &, uint64_t const);
	void dump_account_chain (nano::account const &, std::ostream & = std::cout);
	bool could_fit (store::transaction const &, nano::block const &) const;
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
	void initialize (nano::generate_cache const &);
};

std::unique_ptr<container_info_component> collect_container_info (ledger & ledger, std::string const & name);
}
