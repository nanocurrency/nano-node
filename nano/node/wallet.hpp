#pragma once

#include <celerix/lib/id_dispenser.hpp>
#include <celerix/lib/lmdbconfig.hpp>
#include <celerix/lib/locks.hpp>
#include <celerix/lib/work.hpp>
#include <celerix/node/openclwork.hpp>
#include <celerix/secure/common.hpp>
#include <celerix/store/component.hpp>
#include <celerix/store/lmdb/lmdb.hpp>
#include <celerix/store/lmdb/wallet_value.hpp>
#include <celerix/store/typed_iterator.hpp>

#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_set>

namespace celerix
{
class node;
class node_config;
class wallets;

// The fan spreads a key out over the heap to decrease the likelihood of it being recovered by memory inspection
class fan final
{
public:
	fan (celerix::raw_key const &, std::size_t);
	void value (celerix::raw_key &);
	void value_set (celerix::raw_key const &);
	std::vector<std::unique_ptr<celerix::raw_key>> values;

private:
	celerix::mutex mutex;
	void value_get (celerix::raw_key &);
};

class kdf final
{
public:
	kdf (unsigned & kdf_work) :
		kdf_work{ kdf_work }
	{
	}
	void phs (celerix::raw_key &, std::string const &, celerix::uint256_union const &);
	celerix::mutex mutex;
	unsigned & kdf_work;
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
	using iterator = store::typed_iterator<celerix::account, celerix::wallet_value>;

public:
	wallet_store (bool &, celerix::kdf &, store::transaction &, store::lmdb::env &, celerix::account, unsigned, std::string const &);
	wallet_store (bool &, celerix::kdf &, store::transaction &, store::lmdb::env &, celerix::account, unsigned, std::string const &, std::string const &);
	std::vector<celerix::account> accounts (store::transaction const &);
	void initialize (store::transaction const &, bool &, std::string const &);
	celerix::uint256_union check (store::transaction const &);
	bool rekey (store::transaction const &, std::string const &);
	bool valid_password (store::transaction const &);
	bool valid_public_key (celerix::public_key const &);
	bool attempt_password (store::transaction const &, std::string const &);
	void wallet_key (celerix::raw_key &, store::transaction const &);
	void seed (celerix::raw_key &, store::transaction const &);
	void seed_set (store::transaction const &, celerix::raw_key const &);
	celerix::key_type key_type (celerix::wallet_value const &);
	celerix::public_key deterministic_insert (store::transaction const &);
	celerix::public_key deterministic_insert (store::transaction const &, uint32_t const);
	celerix::raw_key deterministic_key (store::transaction const &, uint32_t);
	uint32_t deterministic_index_get (store::transaction const &);
	void deterministic_index_set (store::transaction const &, uint32_t);
	void deterministic_clear (store::transaction const &);
	celerix::uint256_union salt (store::transaction const &);
	bool is_representative (store::transaction const &);
	celerix::account representative (store::transaction const &);
	void representative_set (store::transaction const &, celerix::account const &);
	celerix::public_key insert_adhoc (store::transaction const &, celerix::raw_key const &);
	bool insert_watch (store::transaction const &, celerix::account const &);
	void erase (store::transaction const &, celerix::account const &);
	celerix::wallet_value entry_get_raw (store::transaction const &, celerix::account const &);
	void entry_put_raw (store::transaction const &, celerix::account const &, celerix::wallet_value const &);
	bool fetch (store::transaction const &, celerix::account const &, celerix::raw_key &);
	bool exists (store::transaction const &, celerix::account const &);
	void destroy (store::transaction const &);
	iterator find (store::transaction const &, celerix::account const &);
	iterator begin (store::transaction const &, celerix::account const &);
	iterator begin (store::transaction const &);
	iterator end (store::transaction const &);
	void derive_key (celerix::raw_key &, store::transaction const &, std::string const &);
	void serialize_json (store::transaction const &, std::string &);
	void write_backup (store::transaction const &, std::filesystem::path const &);
	bool move (store::transaction const &, celerix::wallet_store &, std::vector<celerix::public_key> const &);
	bool import (store::transaction const &, celerix::wallet_store &);
	bool work_get (store::transaction const &, celerix::public_key const &, uint64_t &);
	void work_put (store::transaction const &, celerix::public_key const &, uint64_t);
	unsigned version (store::transaction const &);
	void version_put (store::transaction const &, unsigned);
	celerix::fan password;
	celerix::fan wallet_key_mem;
	static unsigned const version_1 = 1;
	static unsigned const version_2 = 2;
	static unsigned const version_3 = 3;
	static unsigned const version_4 = 4;
	static unsigned constexpr version_current = version_4;
	static celerix::account const version_special;
	static celerix::account const wallet_key_special;
	static celerix::account const salt_special;
	static celerix::account const check_special;
	static celerix::account const representative_special;
	static celerix::account const seed_special;
	static celerix::account const deterministic_index_special;
	static std::size_t const check_iv_index;
	static std::size_t const seed_iv_index;
	static int const special_count;
	celerix::kdf & kdf;
	std::atomic<MDB_dbi> handle{ 0 };
	std::recursive_mutex mutex;

private:
	celerix::store::lmdb::env & env;
};

// A wallet is a set of account keys encrypted by a common encryption key
class wallet final : public std::enable_shared_from_this<celerix::wallet>
{
public:
	std::shared_ptr<celerix::block> change_action (celerix::account const &, celerix::account const &, uint64_t = 0, bool = true);
	std::shared_ptr<celerix::block> receive_action (celerix::block_hash const &, celerix::account const &, celerix::uint128_union const &, celerix::account const &, uint64_t = 0, bool = true);
	std::shared_ptr<celerix::block> send_action (celerix::account const &, celerix::account const &, celerix::uint128_t const &, uint64_t = 0, bool = true, boost::optional<std::string> = {});
	bool action_complete (std::shared_ptr<celerix::block> const &, celerix::account const &, bool const, celerix::block_details const &);
	wallet (bool &, store::transaction &, celerix::wallets &, std::string const &);
	wallet (bool &, store::transaction &, celerix::wallets &, std::string const &, std::string const &);
	void enter_initial_password ();
	bool enter_password (store::transaction const &, std::string const &);
	celerix::public_key insert_adhoc (celerix::raw_key const &, bool = true);
	bool insert_watch (store::transaction const &, celerix::public_key const &);
	celerix::public_key deterministic_insert (store::transaction const &, bool = true);
	celerix::public_key deterministic_insert (uint32_t, bool = true);
	celerix::public_key deterministic_insert (bool = true);
	bool exists (celerix::public_key const &);
	bool import (std::string const &, std::string const &);
	void serialize (std::string &);
	bool change_sync (celerix::account const &, celerix::account const &);
	void change_async (celerix::account const &, celerix::account const &, std::function<void (std::shared_ptr<celerix::block> const &)> const &, uint64_t = 0, bool = true);
	bool receive_sync (std::shared_ptr<celerix::block> const &, celerix::account const &, celerix::uint128_t const &);
	void receive_async (celerix::block_hash const &, celerix::account const &, celerix::uint128_t const &, celerix::account const &, std::function<void (std::shared_ptr<celerix::block> const &)> const &, uint64_t = 0, bool = true);
	celerix::block_hash send_sync (celerix::account const &, celerix::account const &, celerix::uint128_t const &);
	void send_async (celerix::account const &, celerix::account const &, celerix::uint128_t const &, std::function<void (std::shared_ptr<celerix::block> const &)> const &, uint64_t = 0, bool = true, boost::optional<std::string> = {});
	void work_cache_blocking (celerix::account const &, celerix::root const &);
	void work_update (store::transaction const &, celerix::account const &, celerix::root const &, uint64_t);
	// Schedule work generation after a few seconds
	void work_ensure (celerix::account const &, celerix::root const &);
	bool search_receivable (store::transaction const &);
	void init_free_accounts (store::transaction const &);
	uint32_t deterministic_check (store::transaction const & transaction_a, uint32_t index);
	/** Changes the wallet seed and returns the first account */
	celerix::public_key change_seed (store::transaction const & transaction_a, celerix::raw_key const & prv_a, uint32_t count = 0);
	void deterministic_restore (store::transaction const & transaction_a);
	bool live ();
	std::unordered_set<celerix::account> free_accounts;
	std::function<void (bool, bool)> lock_observer;
	celerix::wallet_store store;
	celerix::wallets & wallets;
	celerix::logger & logger;
	celerix::mutex representatives_mutex;
	std::unordered_set<celerix::account> representatives;
};

class wallet_representatives
{
public:
	uint64_t voting{ 0 }; // Number of representatives with at least the configured minimum voting weight
	bool half_principal{ false }; // has representatives with at least 50% of principal representative requirements
	std::unordered_set<celerix::account> accounts; // Representatives with at least the configured minimum voting weight
	bool have_half_rep () const
	{
		return half_principal;
	}
	bool exists (celerix::account const & rep_a) const
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

/**
 * The wallets set is all the wallets a node controls.
 * A node may contain multiple wallets independently encrypted and operated.
 */
class wallets final
{
public:
	wallets (bool error, celerix::node &);
	~wallets ();

	void start ();
	void stop ();

	std::shared_ptr<celerix::wallet> open (celerix::wallet_id const &);
	std::shared_ptr<celerix::wallet> create (celerix::wallet_id const &);
	bool search_receivable (celerix::wallet_id const &);
	void search_receivable_all ();
	void destroy (celerix::wallet_id const &);
	void reload ();
	void do_wallet_actions ();
	void queue_wallet_action (celerix::uint128_t const &, std::shared_ptr<celerix::wallet> const &, std::function<void (celerix::wallet &)>);
	void foreach_representative (std::function<void (celerix::public_key const &, celerix::raw_key const &)> const &);
	bool exists (store::transaction const &, celerix::account const &);
	void clear_send_ids (store::transaction const &);
	celerix::wallet_representatives reps () const;
	bool check_rep (celerix::account const &, celerix::uint128_t const &, bool const = true);
	void compute_reps ();
	void ongoing_compute_reps ();
	void receive_confirmed (celerix::block_hash const & hash_a, celerix::account const & destination_a);
	std::unordered_map<celerix::wallet_id, std::shared_ptr<celerix::wallet>> get_wallets ();
	celerix::container_info container_info () const;

	celerix::network_params & network_params;
	std::function<void (bool)> observer;
	std::unordered_map<celerix::wallet_id, std::shared_ptr<celerix::wallet>> items;
	std::multimap<celerix::uint128_t, std::pair<std::shared_ptr<celerix::wallet>, std::function<void (celerix::wallet &)>>, std::greater<celerix::uint128_t>> actions;
	celerix::locked<std::unordered_map<celerix::account, celerix::root>> delayed_work;
	mutable celerix::mutex mutex;
	mutable celerix::mutex action_mutex;
	celerix::condition_variable condition;
	celerix::kdf kdf;
	MDB_dbi handle;
	MDB_dbi send_action_ids;
	celerix::node & node;
	celerix::logger & logger;
	celerix::store::lmdb::env & env;
	std::atomic<bool> stopped;
	std::thread thread;
	static celerix::uint128_t const generate_priority;
	static celerix::uint128_t const high_priority;

	/** Start read-write transaction */
	store::write_transaction tx_begin_write ();
	/** Start read-only transaction */
	store::read_transaction tx_begin_read ();

private:
	mutable celerix::mutex reps_cache_mutex;
	celerix::wallet_representatives representatives;
};

class wallets_store
{
public:
	virtual ~wallets_store () = default;
	virtual bool init_error () const = 0;
};

class mdb_wallets_store final : public wallets_store
{
public:
	mdb_wallets_store (std::filesystem::path const &, celerix::lmdb_config const & lmdb_config_a = celerix::lmdb_config{});
	celerix::store::lmdb::env environment;
	bool init_error () const override;
	bool error{ false };
};
}
