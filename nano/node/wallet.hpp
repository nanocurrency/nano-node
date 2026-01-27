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
	fan (nano::raw_key const &, std::size_t);
	void value (nano::raw_key &);
	void value_set (nano::raw_key const &);
	std::vector<std::unique_ptr<nano::raw_key>> values;

private:
	nano::mutex mutex;
	void value_get (nano::raw_key &);
};

class kdf final
{
public:
	kdf (unsigned const & kdf_work) :
		kdf_work{ kdf_work }
	{
	}
	void phs (nano::raw_key &, std::string const &, nano::uint256_union const &);
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
	wallet_store (bool &, nano::kdf &, nano::store::write_transaction &, store::lmdb::env &, nano::account, unsigned, std::string const &);
	wallet_store (bool &, nano::kdf &, nano::store::write_transaction &, store::lmdb::env &, nano::account, unsigned, std::string const &, std::string const &);
	void initialize (nano::store::write_transaction const &, bool &, std::string const &);
	std::vector<nano::account> accounts (nano::store::transaction const &);
	nano::uint256_union check (nano::store::transaction const &);
	bool rekey (nano::store::write_transaction const &, std::string const &);
	bool valid_password (nano::store::transaction const &);
	bool valid_public_key (nano::public_key const &);
	bool attempt_password (nano::store::transaction const &, std::string const &);
	void wallet_key (nano::raw_key &, nano::store::transaction const &);
	void seed (nano::raw_key &, nano::store::transaction const &);
	void seed_set (nano::store::write_transaction const &, nano::raw_key const &);
	nano::key_type key_type (nano::wallet_value const &);
	nano::public_key deterministic_insert (nano::store::write_transaction const &);
	nano::public_key deterministic_insert (nano::store::write_transaction const &, uint32_t const);
	nano::raw_key deterministic_key (nano::store::transaction const &, uint32_t);
	uint32_t deterministic_index_get (nano::store::transaction const &);
	void deterministic_index_set (nano::store::write_transaction const &, uint32_t);
	void deterministic_clear (nano::store::write_transaction const &);
	nano::uint256_union salt (nano::store::transaction const &);
	bool is_representative (nano::store::transaction const &);
	nano::account representative (nano::store::transaction const &);
	void representative_set (nano::store::write_transaction const &, nano::account const &);
	nano::public_key insert_adhoc (nano::store::write_transaction const &, nano::raw_key const &);
	bool insert_watch (nano::store::write_transaction const &, nano::account const &);
	void erase (nano::store::write_transaction const &, nano::account const &);
	nano::wallet_value entry_get_raw (nano::store::transaction const &, nano::account const &);
	void entry_put_raw (nano::store::write_transaction const &, nano::account const &, nano::wallet_value const &);
	bool fetch (nano::store::transaction const &, nano::account const &, nano::raw_key &);
	bool exists (nano::store::transaction const &, nano::account const &);
	void destroy (nano::store::write_transaction const &);
	iterator find (nano::store::transaction const &, nano::account const &);
	iterator begin (nano::store::transaction const &, nano::account const &);
	iterator begin (nano::store::transaction const &);
	iterator end (nano::store::transaction const &);
	void derive_key (nano::raw_key &, nano::store::transaction const &, std::string const &);
	void serialize_json (nano::store::transaction const &, std::string &);
	void write_backup (nano::store::transaction const &, std::filesystem::path const &);
	bool move (nano::store::write_transaction const &, nano::wallet_store &, std::vector<nano::public_key> const &);
	bool import (nano::store::write_transaction const &, nano::wallet_store &);
	bool work_get (nano::store::transaction const &, nano::public_key const &, uint64_t &);
	void work_put (nano::store::write_transaction const &, nano::public_key const &, uint64_t);
	unsigned version (nano::store::transaction const &);
	void version_put (nano::store::write_transaction const &, unsigned);

public:
	nano::fan password;
	nano::fan wallet_key_mem;
	nano::kdf & kdf;
	std::atomic<MDB_dbi> handle{ 0 };
	std::recursive_mutex mutex;

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
	std::shared_ptr<nano::block> change_action (nano::account const &, nano::account const &, uint64_t = 0, bool = true);
	std::shared_ptr<nano::block> receive_action (nano::block_hash const &, nano::account const &, nano::uint128_union const &, nano::account const &, uint64_t = 0, bool = true);
	std::shared_ptr<nano::block> send_action (nano::account const &, nano::account const &, nano::uint128_t const &, uint64_t = 0, bool = true, boost::optional<std::string> = {});
	bool action_complete (std::shared_ptr<nano::block> const &, nano::account const &, bool const, nano::block_details const &);
	wallet (bool &, nano::store::write_transaction &, nano::wallets &, std::string const &);
	wallet (bool &, nano::store::write_transaction &, nano::wallets &, std::string const &, std::string const &);
	void enter_initial_password ();
	bool enter_password (nano::store::transaction const &, std::string const &);
	nano::public_key insert_adhoc (nano::raw_key const &, bool = true);
	bool insert_watch (nano::store::write_transaction const &, nano::public_key const &);
	nano::public_key deterministic_insert (nano::store::write_transaction const &, bool = true);
	nano::public_key deterministic_insert (uint32_t, bool = true);
	nano::public_key deterministic_insert (bool = true);
	bool exists (nano::public_key const &);
	bool import (std::string const &, std::string const &);
	void serialize (std::string &);
	bool change_sync (nano::account const &, nano::account const &);
	void change_async (nano::account const &, nano::account const &, std::function<void (std::shared_ptr<nano::block> const &)> const &, uint64_t = 0, bool = true);
	bool receive_sync (std::shared_ptr<nano::block> const &, nano::account const &, nano::uint128_t const &);
	void receive_async (nano::block_hash const &, nano::account const &, nano::uint128_t const &, nano::account const &, std::function<void (std::shared_ptr<nano::block> const &)> const &, uint64_t = 0, bool = true);
	nano::block_hash send_sync (nano::account const &, nano::account const &, nano::uint128_t const &);
	void send_async (nano::account const &, nano::account const &, nano::uint128_t const &, std::function<void (std::shared_ptr<nano::block> const &)> const &, uint64_t = 0, bool = true, boost::optional<std::string> = {});
	void work_cache_blocking (nano::account const &, nano::root const &);
	void work_update (nano::store::write_transaction const &, nano::account const &, nano::root const &, uint64_t);
	// Schedule work generation after a few seconds
	void work_ensure (nano::account const &, nano::root const &);
	bool search_receivable (nano::store::transaction const &);
	void init_free_accounts (nano::store::transaction const &);
	uint32_t deterministic_check (nano::store::transaction const &, uint32_t index);
	/** Changes the wallet seed and returns the first account */
	nano::public_key change_seed (nano::store::write_transaction const &, nano::raw_key const & prv, uint32_t count = 0);
	void deterministic_restore (nano::store::write_transaction const &);
	bool live ();
	std::unordered_set<nano::account> reps () const;

public:
	std::unordered_set<nano::account> free_accounts;
	std::function<void (bool, bool)> lock_observer;
	nano::wallet_store store;
	nano::wallets & wallets;
	nano::logger & logger;

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

	std::shared_ptr<nano::wallet> open (nano::wallet_id const &);
	std::shared_ptr<nano::wallet> create (nano::wallet_id const &);
	bool search_receivable (nano::wallet_id const &);
	void search_receivable_all ();
	void destroy (nano::wallet_id const &);
	void reload ();
	void do_wallet_actions ();
	void queue_wallet_action (nano::uint128_t const &, std::shared_ptr<nano::wallet> const &, std::function<void (nano::wallet &)>);
	void foreach_representative (std::function<void (nano::public_key const &, nano::raw_key const &)> const &);
	bool exists (nano::store::transaction const &, nano::account const &);
	bool exists (nano::account const &);
	void clear_send_ids ();
	nano::wallet_representatives reps () const;
	bool check_rep (nano::account const &);
	void compute_reps ();
	void receive_confirmed (nano::block_hash const & hash, nano::account const & destination);
	std::unordered_map<nano::wallet_id, std::shared_ptr<nano::wallet>> get_wallets ();
	nano::container_info container_info () const;

	nano::store::write_transaction tx_begin_write ();
	nano::store::read_transaction tx_begin_read ();

private:
	void run_reps_scan ();
	void run_receivable_scan ();
	bool check_rep_impl (wallet_representatives &, nano::account const &, nano::uint128_t const & half_principal_weight);

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
	mutable nano::locked<nano::wallet_representatives> representatives;
};
}
