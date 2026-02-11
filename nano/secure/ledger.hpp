#pragma once

#include <nano/lib/config.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/timer.hpp>
#include <nano/secure/account_info.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/fwd.hpp>
#include <nano/secure/generate_cache_flags.hpp>
#include <nano/secure/pending_info.hpp>
#include <nano/secure/rep_weights.hpp>
#include <nano/secure/transaction.hpp>
#include <nano/store/ledger_store.hpp>
#include <nano/weights/bootstrap_weights.hpp>

#include <deque>
#include <map>
#include <memory>

namespace nano
{
class ledger;
class ledger_set_any;
class ledger_set_confirmed;

class ledger_cache
{
	friend class ledger;

private:
	std::atomic<uint64_t> cemented_count{ 0 };
	std::atomic<uint64_t> block_count{ 0 };
	std::atomic<uint64_t> pruned_count{ 0 };
	std::atomic<uint64_t> account_count{ 0 };
};

class ledger final
{
	template <typename T>
	friend class receivable_iterator;

public:
	ledger (nano::store::ledger_store &, nano::network_params const &, nano::stats &, nano::logger &, nano::generate_cache_flags = {}, nano::uint128_t min_rep_weight = 0, uint64_t max_backlog = 0);
	~ledger ();

	/** Start read-write transaction */
	secure::write_transaction tx_begin_write (nano::store::writer guard_type = nano::store::writer::generic) const;
	/** Start read-only transaction */
	secure::read_transaction tx_begin_read () const;

	bool unconfirmed_exists (secure::transaction const &, nano::block_hash const &) const;
	nano::uint128_t account_receivable (secure::transaction const &, nano::account const &, bool = false) const;
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
	nano::block_hash representative_block (secure::transaction const &, nano::block_hash const &);
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
	bool is_epoch_link (nano::link const &) const;
	std::shared_ptr<nano::block> find_receive_block_by_send_hash (secure::transaction const &, nano::account const & destination, nano::block_hash const & send_block_hash);
	std::optional<nano::account> linked_account (secure::transaction const &, nano::block const &);
	nano::account const & epoch_signer (nano::link const &) const;
	nano::link const & epoch_link (nano::epoch) const;
	bool bootstrap_height_reached () const;
	std::unordered_map<nano::account, nano::uint128_t> rep_weights_snapshot () const;

	static nano::epoch version (nano::block const & block);
	nano::epoch version (secure::transaction const &, nano::block_hash const & hash) const;

	/**
	 * Checks if all blocks that this block depends on are confirmed (or pruned)
	 */
	bool dependencies_confirmed (secure::transaction const &, nano::block const &) const;

	/**
	 * Computes the priority balance and timestamp for bucket-based prioritization
	 */
	using block_priority_result = std::pair<nano::amount, nano::priority_timestamp>;
	block_priority_result block_priority (secure::transaction const &, nano::block const &) const;

	uint64_t cemented_count () const;
	uint64_t block_count () const;
	uint64_t account_count () const;
	uint64_t pruned_count () const;
	uint64_t backlog_size () const;
	uint64_t max_backlog () const;

	void verify_consistency (secure::transaction const &) const;

	nano::container_info container_info () const;

public:
	static nano::uint128_t const unit;

	nano::ledger_constants const & constants;
	nano::work_thresholds const & work;
	nano::store::ledger_store & store;
	nano::stats & stats;
	nano::logger & logger;

	nano::ledger_cache cache;
	nano::rep_weights rep_weights;

public:
	uint64_t const max_backlog_size{ 0 };
	bool pruning{ false };

	nano::bootstrap_weights bootstrap_weights{};

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
