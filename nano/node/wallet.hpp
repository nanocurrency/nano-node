#pragma once

#include <nano/lib/id_dispenser.hpp>
#include <nano/lib/lmdbconfig.hpp>
#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/numbers_templ.hpp>
#include <nano/lib/thread_pool.hpp>
#include <nano/lib/work.hpp>
#include <nano/node/fwd.hpp>
#include <nano/node/openclwork.hpp>
#include <nano/secure/common.hpp>
#include <nano/store/lmdb/lmdb_env.hpp>
#include <nano/store/lmdb/wallet_value.hpp>
#include <nano/store/typed_iterator.hpp>

#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_set>

namespace nano
{
class node;
class node_config;
class wallets;

// The fan spreads a key out over the heap to decrease the likelihood of it being recovered by memory inspection
class fan final
{
public:
	fan (nano::raw_key const & key, std::size_t count);
	void value (nano::raw_key & result) const;
	void value_set (nano::raw_key const & value);
	std::vector<std::unique_ptr<nano::raw_key>> values;

private:
	mutable nano::mutex mutex;
	void value_get (nano::raw_key & result) const;
};

class kdf final
{
public:
	kdf (unsigned const & kdf_work) :
		kdf_work{ kdf_work }
	{
	}
	void phs (nano::raw_key & result, std::string const & password, nano::uint256_union const & salt);
	nano::mutex mutex;
	unsigned const & kdf_work;
};

enum class key_type
{
	not_a_type,
	unknown,
	adhoc,
	deterministic
};

class wallet_store final
{
public:
	using iterator = store::typed_iterator<nano::account, nano::wallet_value>;

public:
	wallet_store (bool & error, nano::kdf &, nano::store::write_transaction &, store::lmdb::env &, nano::account representative, unsigned fanout, std::string const & wallet_path);
	wallet_store (bool & error, nano::kdf &, nano::store::write_transaction &, store::lmdb::env &, nano::account representative, unsigned fanout, std::string const & wallet_path, std::string const & json);
	void initialize (nano::store::write_transaction const &, bool & error, std::string const & path);
	std::vector<nano::account> accounts (nano::store::transaction const &) const;
	nano::uint256_union check (nano::store::transaction const &) const;
	bool rekey (nano::store::write_transaction const &, std::string const & password);
	bool valid_password (nano::store::transaction const &) const;
	bool valid_public_key (nano::public_key const &) const;
	bool attempt_password (nano::store::transaction const &, std::string const & password);
	void wallet_key (nano::raw_key & result, nano::store::transaction const &) const;
	void seed (nano::raw_key & result, nano::store::transaction const &) const;
	void seed_set (nano::store::write_transaction const &, nano::raw_key const & seed);
	nano::key_type key_type (nano::wallet_value const &) const;
	nano::public_key deterministic_insert (nano::store::write_transaction const &);
	nano::public_key deterministic_insert (nano::store::write_transaction const &, uint32_t index);
	nano::raw_key deterministic_key (nano::store::transaction const &, uint32_t index) const;
	uint32_t deterministic_index_get (nano::store::transaction const &) const;
	void deterministic_index_set (nano::store::write_transaction const &, uint32_t index);
	void deterministic_clear (nano::store::write_transaction const &);
	nano::uint256_union salt (nano::store::transaction const &) const;
	bool is_representative (nano::store::transaction const &) const;
	nano::account representative (nano::store::transaction const &) const;
	void representative_set (nano::store::write_transaction const &, nano::account const & rep);
	nano::public_key insert_adhoc (nano::store::write_transaction const &, nano::raw_key const & prv);
	bool insert_watch (nano::store::write_transaction const &, nano::account const & pub);
	void erase (nano::store::write_transaction const &, nano::account const & pub);
	nano::wallet_value entry_get_raw (nano::store::transaction const &, nano::account const & pub) const;
	void entry_put_raw (nano::store::write_transaction const &, nano::account const & pub, nano::wallet_value const & entry);
	bool fetch (nano::store::transaction const &, nano::account const & pub, nano::raw_key & result) const;
	bool exists (nano::store::transaction const &, nano::account const & pub) const;
	void destroy (nano::store::write_transaction const &);
	iterator find (nano::store::transaction const &, nano::account const & key) const;
	iterator begin (nano::store::transaction const &, nano::account const & key) const;
	iterator begin (nano::store::transaction const &) const;
	iterator end (nano::store::transaction const &) const;
	void derive_key (nano::raw_key & result, nano::store::transaction const &, std::string const & password) const;
	void serialize_json (nano::store::transaction const &, std::string & json) const;
	void write_backup (nano::store::transaction const &, std::filesystem::path const & path) const;
	bool move (nano::store::write_transaction const &, nano::wallet_store & source, std::vector<nano::public_key> const & keys);
	bool import (nano::store::write_transaction const &, nano::wallet_store & source);
	bool work_get (nano::store::transaction const &, nano::public_key const & pub, uint64_t & work) const;
	void work_put (nano::store::write_transaction const &, nano::public_key const & pub, uint64_t work);
	unsigned version (nano::store::transaction const &) const;
	void version_put (nano::store::write_transaction const &, unsigned version);

public:
	nano::fan password;
	nano::fan wallet_key_mem;
	nano::kdf & kdf;
	std::atomic<MDB_dbi> handle{ 0 };
	mutable std::recursive_mutex mutex;

private:
	nano::store::lmdb::env & env;

public:
	static unsigned const version_1 = 1;
	static unsigned const version_2 = 2;
	static unsigned const version_3 = 3;
	static unsigned const version_4 = 4;
	static unsigned constexpr version_current = version_4;
	static nano::account const version_special;
	static nano::account const wallet_key_special;
	static nano::account const salt_special;
	static nano::account const check_special;
	static nano::account const representative_special;
	static nano::account const seed_special;
	static nano::account const deterministic_index_special;
	static std::size_t const check_iv_index;
	static std::size_t const seed_iv_index;
	static int const special_count;
};

// A wallet is a set of account keys encrypted by a common encryption key
class wallet final : public std::enable_shared_from_this<nano::wallet>
{
public:
	wallet (bool & error, nano::store::write_transaction &, nano::wallets &, std::string const & wallet_path);
	wallet (bool & error, nano::store::write_transaction &, nano::wallets &, std::string const & wallet_path, std::string const & json);

	// Password and lock management
	void enter_initial_password ();
	bool enter_password (std::string const & password);
	bool rekey (std::string const & password);
	bool is_locked () const;
	void lock ();

	// Account management
	nano::public_key insert_adhoc (nano::raw_key const & prv, bool generate_work = true);
	nano::public_key deterministic_insert (uint32_t index, bool generate_work = true);
	nano::public_key deterministic_insert (bool generate_work = true);
	bool insert_watch (nano::public_key const & pub);
	void remove_account (nano::account const & account);
	std::vector<nano::account> accounts () const;
	bool exists (nano::public_key const & pub);
	bool move_accounts (wallet & source, std::vector<nano::public_key> const & accounts);
	nano::key_type key_type (nano::account const & account) const;

	// Seed management
	bool get_seed (nano::raw_key & result) const;
	nano::public_key change_seed (nano::raw_key const & seed, uint32_t count = 0);
	void deterministic_restore ();
	uint32_t deterministic_check (uint32_t index);
	uint32_t get_deterministic_index () const;

	// Representative management
	void set_representative (nano::account const & rep);
	nano::account get_representative () const;

	// Local wallet representatives
	std::unordered_set<nano::account> reps () const;

	// Key retrieval
	bool fetch_prv (nano::account const & pub, nano::raw_key & result) const;

	// Block actions
	std::shared_ptr<nano::block> change_action (nano::account const & source, nano::account const & representative, uint64_t work = 0, bool generate_work = true);
	std::shared_ptr<nano::block> receive_action (nano::block_hash const & send_hash, nano::account const & representative, nano::uint128_union const & amount, nano::account const & account, uint64_t work = 0, bool generate_work = true);
	std::shared_ptr<nano::block> send_action (nano::account const & source, nano::account const & destination, nano::uint128_t const & amount, uint64_t work = 0, bool generate_work = true, boost::optional<std::string> id = {});
	bool action_complete (std::shared_ptr<nano::block> const & block, nano::account const & account, bool generate_work, nano::block_details const & details);

	// Sync/async block operations
	bool change_sync (nano::account const & source, nano::account const & representative);
	void change_async (nano::account const & source, nano::account const & representative, std::function<void (std::shared_ptr<nano::block> const &)> const & action, uint64_t work = 0, bool generate_work = true);
	bool receive_sync (std::shared_ptr<nano::block> const & block, nano::account const & representative, nano::uint128_t const & amount);
	void receive_async (nano::block_hash const & hash, nano::account const & representative, nano::uint128_t const & amount, nano::account const & account, std::function<void (std::shared_ptr<nano::block> const &)> const & action, uint64_t work = 0, bool generate_work = true);
	nano::block_hash send_sync (nano::account const & source, nano::account const & destination, nano::uint128_t const & amount);
	void send_async (nano::account const & source, nano::account const & destination, nano::uint128_t const & amount, std::function<void (std::shared_ptr<nano::block> const &)> const & action, uint64_t work = 0, bool generate_work = true, boost::optional<std::string> id = {});

	// Work cache
	void work_cache_blocking (nano::account const & account, nano::root const & root);
	void work_ensure (nano::account const & account, nano::root const & root);
	bool get_work (nano::public_key const & pub, uint64_t & work) const;
	void set_work (nano::public_key const & pub, uint64_t work);

	// Receivable
	bool search_receivable ();

	// Import/export
	bool import (std::string const & json, std::string const & password);
	void serialize_json (std::string & json);
	void write_backup (std::filesystem::path const & path);

	// Status
	bool live ();

public:
	std::unordered_set<nano::account> free_accounts;
	std::function<void (bool, bool)> lock_observer;
	nano::wallet_store store;
	nano::wallets & wallets;
	nano::logger & logger;

private:
	// Internal implementation methods (accept transactions for batching scenarios)
	bool enter_password_impl (nano::store::transaction const &, std::string const & password);
	bool insert_watch_impl (nano::store::write_transaction const &, nano::public_key const & pub);
	nano::public_key deterministic_insert_impl (nano::store::write_transaction const &, bool generate_work = true);
	void work_update_impl (nano::store::write_transaction const &, nano::account const & account, nano::root const & root, uint64_t work);
	bool search_receivable_impl (nano::store::transaction const &);
	void init_free_accounts_impl (nano::store::transaction const &);
	uint32_t deterministic_check_impl (nano::store::transaction const &, uint32_t index);
	nano::public_key change_seed_impl (nano::store::write_transaction const &, nano::raw_key const & seed, uint32_t count = 0);
	void deterministic_restore_impl (nano::store::write_transaction const &);

private:
	nano::locked<std::unordered_set<nano::account>> representatives;

	friend class wallets;
};

class wallet_representatives
{
public:
	uint64_t voting{ 0 }; // Number of representatives with at least the configured minimum voting weight
	bool half_principal{ false }; // has representatives with at least 50% of principal representative requirements
	std::unordered_set<nano::account> accounts; // Representatives with at least the configured minimum voting weight
	bool have_half_rep () const
	{
		return half_principal;
	}
	bool exists (nano::account const & rep_a) const
	{
		return accounts.count (rep_a) > 0;
	}
	void clear ()
	{
		voting = 0;
		half_principal = false;
		accounts.clear ();
	}
};

class wallets_store
{
public:
	virtual ~wallets_store () = default;
};

class mdb_wallets_store final : public wallets_store
{
public:
	mdb_wallets_store (std::filesystem::path const &, nano::lmdb_config const & lmdb_config_a = nano::lmdb_config{});
	nano::store::lmdb::env environment;
	bool error{ false };
};

/**
 * The wallets set is all the wallets a node controls.
 * A node may contain multiple wallets independently encrypted and operated.
 */
class wallets final
{
public:
	wallets (
	nano::node &,
	nano::wallets_store &,
	nano::ledger &,
	nano::node_config const &,
	nano::network_params const &,
	nano::online_reps &,
	nano::network &,
	nano::stats &,
	nano::logger &);

	~wallets ();

	void start ();
	void stop ();

	// Wallet management
	std::shared_ptr<nano::wallet> open (nano::wallet_id const &);
	std::shared_ptr<nano::wallet> create (nano::wallet_id const &);
	std::shared_ptr<nano::wallet> create_from_json (nano::wallet_id const &, std::string const & json);
	void destroy (nano::wallet_id const &);
	void reload ();
	void clear_send_ids ();

	// Account lookup
	std::unordered_map<nano::wallet_id, std::shared_ptr<nano::wallet>> all_wallets ();
	bool exists (nano::account const &);
	bool exists_any (nano::account const &, nano::account const &);

	// Receivable
	bool search_receivable (nano::wallet_id const &);
	void search_receivable_all ();
	void receive_confirmed (nano::block_hash const & hash, nano::account const & destination);

	// Wallet actions queue
	void do_wallet_actions ();
	void queue_wallet_action (nano::uint128_t const & priority, std::shared_ptr<nano::wallet> const &, std::function<void (nano::wallet &)> action);

	// Representatives
	void foreach_representative (std::function<void (nano::public_key const &, nano::raw_key const &)> const & action);
	bool check_rep (nano::account const &);
	void compute_reps ();
	nano::wallet_representatives reps () const;

	nano::container_info container_info () const;

private: // Transactions
	nano::store::write_transaction tx_begin_write ();
	nano::store::read_transaction tx_begin_read ();

public: // Dependencies
	nano::node & node;
	nano::wallets_store & wallets_store;
	nano::ledger & ledger;
	nano::node_config const & config;
	nano::network_params const & network_params;
	nano::online_reps & online_reps;
	nano::network & network;
	nano::stats & stats;
	nano::logger & logger;

public:
	std::function<void (bool)> observer;

	std::unordered_map<nano::wallet_id, std::shared_ptr<nano::wallet>> items;
	std::multimap<nano::uint128_t, std::pair<std::shared_ptr<nano::wallet>, std::function<void (nano::wallet &)>>, std::greater<nano::uint128_t>> actions;
	nano::locked<std::unordered_map<nano::account, nano::root>> delayed_work;

	nano::kdf kdf;

	MDB_dbi handle{};
	MDB_dbi send_action_ids{};
	nano::store::lmdb::env & env;

	mutable nano::mutex mutex;
	mutable nano::mutex action_mutex;
	nano::condition_variable condition;
	nano::condition_variable reps_condition;
	nano::condition_variable receivable_condition;
	std::atomic<bool> stopped{ false };
	std::thread thread;
	std::thread reps_thread;
	std::thread receivable_thread;

	nano::thread_pool workers;

	static nano::uint128_t const generate_priority;
	static nano::uint128_t const high_priority;

private:
	void run_reps_scan ();
	void run_receivable_scan ();
	bool check_rep_impl (wallet_representatives &, nano::account const &, nano::uint128_t const & half_principal_weight);
	bool exists_impl (nano::store::transaction const &, nano::account const &);

private:
	mutable nano::locked<nano::wallet_representatives> representatives;

	friend class wallet;
};
}
