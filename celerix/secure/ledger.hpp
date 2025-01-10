#pragma once

#include <celerix/lib/numbers.hpp>
#include <celerix/lib/timer.hpp>
#include <celerix/secure/account_info.hpp>
#include <celerix/secure/generate_cache_flags.hpp>
#include <celerix/secure/ledger_cache.hpp>
#include <celerix/secure/pending_info.hpp>
#include <celerix/secure/transaction.hpp>

#include <deque>
#include <map>
#include <memory>

namespace celerix::store
{
class component;
}

namespace celerix
{
class block;
enum class block_status;
enum class epoch : uint8_t;
class ledger_constants;
class ledger_set_any;
class ledger_set_confirmed;
class pending_info;
class pending_key;
class stats;

class ledger final
{
	template <typename T>
	friend class receivable_iterator;

public:
	ledger (celerix::store::component &, celerix::stats &, celerix::ledger_constants & constants, celerix::generate_cache_flags const & = celerix::generate_cache_flags{}, celerix::uint128_t min_rep_weight_a = 0);
	~ledger ();

	/** Start read-write transaction */
	secure::write_transaction tx_begin_write (celerix::store::writer guard_type = celerix::store::writer::generic) const;
	/** Start read-only transaction */
	secure::read_transaction tx_begin_read () const;

	bool unconfirmed_exists (secure::transaction const &, celerix::block_hash const &);
	celerix::uint128_t account_receivable (secure::transaction const &, celerix::account const &, bool = false);
	/**
	 * Returns the cached vote weight for the given representative.
	 * If the weight is below the cache limit it returns 0.
	 * During bootstrap it returns the preconfigured bootstrap weights.
	 */
	celerix::uint128_t weight (celerix::account const &) const;
	/* Returns the exact vote weight for the given representative by doing a database lookup */
	celerix::uint128_t weight_exact (secure::transaction const &, celerix::account const &) const;
	std::shared_ptr<celerix::block> forked_block (secure::transaction const &, celerix::block const &);
	celerix::root latest_root (secure::transaction const &, celerix::account const &);
	celerix::block_hash representative (secure::transaction const &, celerix::block_hash const &);
	celerix::block_hash representative_calculated (secure::transaction const &, celerix::block_hash const &);
	std::string block_text (char const *);
	std::string block_text (celerix::block_hash const &);
	std::deque<std::shared_ptr<celerix::block>> random_blocks (secure::transaction const &, size_t count) const;
	std::optional<celerix::pending_info> pending_info (secure::transaction const &, celerix::pending_key const & key) const;
	std::deque<std::shared_ptr<celerix::block>> confirm (secure::write_transaction &, celerix::block_hash const & hash, size_t max_blocks = 1024 * 128);
	celerix::block_status process (secure::write_transaction const &, std::shared_ptr<celerix::block> block);
	bool rollback (secure::write_transaction const &, celerix::block_hash const &, std::deque<std::shared_ptr<celerix::block>> & rollback_list);
	bool rollback (secure::write_transaction const &, celerix::block_hash const &);
	void update_account (secure::write_transaction const &, celerix::account const &, celerix::account_info const &, celerix::account_info const &);
	uint64_t pruning_action (secure::write_transaction &, celerix::block_hash const &, uint64_t const);
	void dump_account_chain (celerix::account const &, std::ostream & = std::cout);
	bool dependents_confirmed (secure::transaction const &, celerix::block const &) const;
	bool is_epoch_link (celerix::link const &) const;
	std::array<celerix::block_hash, 2> dependent_blocks (secure::transaction const &, celerix::block const &) const;
	std::shared_ptr<celerix::block> find_receive_block_by_send_hash (secure::transaction const &, celerix::account const & destination, celerix::block_hash const & send_block_hash);
	celerix::account const & epoch_signer (celerix::link const &) const;
	celerix::link const & epoch_link (celerix::epoch) const;
	bool migrate_lmdb_to_rocksdb (std::filesystem::path const &) const;
	bool bootstrap_weight_reached () const;

	static celerix::epoch version (celerix::block const & block);
	celerix::epoch version (secure::transaction const &, celerix::block_hash const & hash) const;

	uint64_t cemented_count () const;
	uint64_t block_count () const;
	uint64_t account_count () const;
	uint64_t pruned_count () const;
	uint64_t backlog_count () const;

	// Returned priority balance is maximum of block balance and previous block balance to handle full account balance send cases
	// Returned timestamp is the previous block timestamp or the current timestamp if there's no previous block
	using block_priority_result = std::pair<celerix::amount, celerix::priority_timestamp>;
	block_priority_result block_priority (secure::transaction const &, celerix::block const &) const;

	celerix::container_info container_info () const;

public:
	static celerix::uint128_t const unit;

	celerix::ledger_constants & constants;
	celerix::store::component & store;
	celerix::ledger_cache cache;
	celerix::stats & stats;

	std::unordered_map<celerix::account, celerix::uint128_t> bootstrap_weights;
	uint64_t bootstrap_weight_max_blocks{ 1 };
	mutable std::atomic<bool> check_bootstrap_weights;

	bool pruning{ false };

private:
	void initialize (celerix::generate_cache_flags const &);
	void confirm_one (secure::write_transaction &, celerix::block const & block);

	std::unique_ptr<ledger_set_any> any_impl;
	std::unique_ptr<ledger_set_confirmed> confirmed_impl;

public:
	ledger_set_any & any;
	ledger_set_confirmed & confirmed;
};
}
