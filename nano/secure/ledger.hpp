#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/timer.hpp>
#include <nano/secure/account_info.hpp>
#include <nano/secure/generate_cache_flags.hpp>
#include <nano/secure/ledger_cache.hpp>
#include <nano/secure/pending_info.hpp>
#include <nano/secure/unconfirmed_set.hpp>

#include <deque>
#include <map>
#include <memory>

namespace nano::store
{
class component;
class transaction;
class write_transaction;
}

namespace nano
{
class block;
class block_delta;
enum class block_status;
enum class epoch : uint8_t;
class ledger_constants;
class ledger_view_confirmed;
class ledger_view_unconfirmed;
class pending_info;
class pending_key;
class stats;

class ledger final
{
	friend class ledger_view_unconfirmed;
	template <typename T>
	friend class receivable_iterator;

public:
	ledger (nano::store::component &, nano::stats &, nano::ledger_constants & constants, nano::generate_cache_flags const & = nano::generate_cache_flags{}, nano::uint128_t min_rep_weight_a = 0);
	~ledger ();

	ledger_view_unconfirmed * operator->() const;
	ledger_view_confirmed & confirmed () const;
	ledger_view_unconfirmed & unconfirmed () const;

	nano::uint128_t account_receivable (store::transaction const &, nano::account const &, bool = false);
	/**
	 * Returns the cached vote weight for the given representative.
	 * If the weight is below the cache limit it returns 0.
	 * During bootstrap it returns the preconfigured bootstrap weights.
	 */
	nano::uint128_t weight (nano::account const &);
	/* Returns the exact vote weight for the given representative by doing a database lookup */
	nano::uint128_t weight_exact (store::transaction const &, nano::account const &);
	std::shared_ptr<nano::block> forked_block (store::transaction const &, nano::block const &);
	bool confirmed (store::transaction const &, nano::block_hash const &) const;
	nano::root latest_root (store::transaction const &, nano::account const &);
	nano::block_hash representative (store::transaction const &, nano::block_hash const &);
	nano::block_hash representative_calculated (store::transaction const &, nano::block_hash const &);
	std::string block_text (char const *);
	std::string block_text (nano::block_hash const &);
	std::pair<nano::block_hash, nano::block_hash> hash_root_random (store::transaction const &) const;
	std::deque<std::shared_ptr<nano::block>> confirm (nano::store::write_transaction const & transaction, nano::block_hash const & hash);
	nano::block_status process (store::write_transaction const & transaction, std::shared_ptr<nano::block> block);
	bool rollback (store::write_transaction const &, nano::block_hash const &, std::vector<std::shared_ptr<nano::block>> &);
	bool rollback (store::write_transaction const &, nano::block_hash const &);
	uint64_t pruning_action (store::write_transaction &, nano::block_hash const &, uint64_t const);
	void dump_account_chain (nano::account const &, std::ostream & = std::cout);
	bool dependents_confirmed (store::transaction const &, nano::block const &) const;
	bool is_epoch_link (nano::link const &) const;
	std::array<nano::block_hash, 2> dependent_blocks (store::transaction const &, nano::block const &) const;
	std::shared_ptr<nano::block> find_receive_block_by_send_hash (store::transaction const & transaction, nano::account const & destination, nano::block_hash const & send_block_hash);
	nano::account const & epoch_signer (nano::link const &) const;
	nano::link const & epoch_link (nano::epoch) const;
	bool migrate_lmdb_to_rocksdb (std::filesystem::path const &) const;
	bool bootstrap_weight_reached () const;
	static nano::epoch version (nano::block const & block);
	nano::epoch version (store::transaction const & transaction, nano::block_hash const & hash) const;
	nano::account_info account_info (nano::store::transaction const & transaction, nano::block const & block, nano::account const & representative);
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
	void initialize (nano::generate_cache_flags const &);
	void track (store::write_transaction const & transaction, nano::block_delta const & delta);
	void confirm (nano::store::write_transaction const & transaction, nano::block const & block);
	nano::unconfirmed_set unconfirmed_set;

	std::unique_ptr<ledger_view_unconfirmed> unconfirmed_view;
	std::unique_ptr<ledger_view_confirmed> confirmed_view;
};

std::unique_ptr<container_info_component> collect_container_info (ledger & ledger, std::string const & name);
}
