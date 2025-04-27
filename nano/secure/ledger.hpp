#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/timer.hpp>
#include <nano/secure/account_info.hpp>
#include <nano/secure/fwd.hpp>
#include <nano/secure/generate_cache_flags.hpp>
#include <nano/secure/ledger_cache.hpp>
#include <nano/secure/pending_info.hpp>
#include <nano/secure/transaction.hpp>

#include <deque>
#include <map>
#include <memory>

namespace nano::store
{
class component;
}

namespace nano
{
class ledger_set_any;
class ledger_set_confirmed;

class ledger final
{
	template <typename T>
	friend class receivable_iterator;

public:
	ledger (nano::store::component &, nano::ledger_constants &, nano::stats &, nano::logger &, nano::generate_cache_flags const & = nano::generate_cache_flags{}, nano::uint128_t min_rep_weight_a = 0);
	~ledger ();

	/** Start read-write transaction */
	secure::write_transaction tx_begin_write (nano::store::writer guard_type = nano::store::writer::generic) const;
	/** Start read-only transaction */
	secure::read_transaction tx_begin_read () const;

	bool unconfirmed_exists (secure::transaction const &, nano::block_hash const &);
	nano::uint128_t account_receivable (secure::transaction const &, nano::account const &, bool = false);
	/**
	 * Returns the cached vote weight for the given representative.
	 * If the weight is below the cache limit it returns 0.
	 * During bootstrap it returns the preconfigured bootstrap weights.
	 */
	nano::uint128_t weight (nano::account const &) const;
	/* Returns the exact vote weight for the given representative by doing a database lookup */
	nano::uint128_t weight_exact (secure::transaction const &, nano::account const &) const;
	std::shared_ptr<nano::block> forked_block (secure::transaction const &, nano::block const &);
	nano::root latest_root (secure::transaction const &, nano::account const &);
	nano::block_hash representative (secure::transaction const &, nano::block_hash const &);
	nano::block_hash representative_calculated (secure::transaction const &, nano::block_hash const &);
	std::string block_text (char const *);
	std::string block_text (nano::block_hash const &);
	std::deque<std::shared_ptr<nano::block>> random_blocks (secure::transaction const &, size_t count) const;
	std::optional<nano::pending_info> pending_info (secure::transaction const &, nano::pending_key const & key) const;
	std::deque<std::shared_ptr<nano::block>> confirm (secure::write_transaction &, nano::block_hash const & hash, size_t max_blocks = 1024 * 128);
	nano::block_status process (secure::write_transaction const &, std::shared_ptr<nano::block> block);
	bool rollback (secure::write_transaction const &, nano::block_hash const &, std::deque<std::shared_ptr<nano::block>> & rollback_list, size_t depth = 0, size_t max_depth = nano::ledger_max_rollback_depth ());
	bool rollback (secure::write_transaction const &, nano::block_hash const &);
	void update_account (secure::write_transaction const &, nano::account const &, nano::account_info const &, nano::account_info const &);
	uint64_t pruning_action (secure::write_transaction &, nano::block_hash const &, uint64_t const);
	void dump_account_chain (nano::account const &, std::ostream & = std::cout);
	bool dependents_confirmed (secure::transaction const &, nano::block const &) const;
	bool is_epoch_link (nano::link const &) const;
	std::array<nano::block_hash, 2> dependent_blocks (secure::transaction const &, nano::block const &) const;
	std::shared_ptr<nano::block> find_receive_block_by_send_hash (secure::transaction const &, nano::account const & destination, nano::block_hash const & send_block_hash);
	std::optional<nano::account> linked_account (secure::transaction const &, nano::block const &);
	nano::account const & epoch_signer (nano::link const &) const;
	nano::link const & epoch_link (nano::epoch) const;
	bool migrate_lmdb_to_rocksdb (std::filesystem::path const &) const;
	bool bootstrap_height_reached () const;
	std::unordered_map<nano::account, nano::uint128_t> rep_weights_snapshot () const;

	static nano::epoch version (nano::block const & block);
	nano::epoch version (secure::transaction const &, nano::block_hash const & hash) const;

	uint64_t cemented_count () const;
	uint64_t block_count () const;
	uint64_t account_count () const;
	uint64_t pruned_count () const;
	uint64_t backlog_count () const;

	// Returned priority balance is maximum of block balance and previous block balance to handle full account balance send cases
	// Returned timestamp is the previous block timestamp or the current timestamp if there's no previous block
	using block_priority_result = std::pair<nano::amount, nano::priority_timestamp>;
	block_priority_result block_priority (secure::transaction const &, nano::block const &) const;

	nano::container_info container_info () const;

public:
	static nano::uint128_t const unit;

	nano::ledger_constants & constants;
	nano::store::component & store;
	nano::ledger_cache cache;
	nano::stats & stats;
	nano::logger & logger;

	std::unordered_map<nano::account, nano::uint128_t> bootstrap_weights;
	uint64_t bootstrap_weight_max_blocks{ 1 };

	bool pruning{ false };

private:
	void initialize (nano::generate_cache_flags const &);
	void confirm_one (secure::write_transaction &, nano::block const & block);

	std::unique_ptr<ledger_set_any> any_impl;
	std::unique_ptr<ledger_set_confirmed> confirmed_impl;

public:
	ledger_set_any & any;
	ledger_set_confirmed & confirmed;
};
}
