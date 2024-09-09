#pragma once

#include <nano/lib/id_dispenser.hpp>
#include <nano/lib/lmdbconfig.hpp>
#include <nano/lib/locks.hpp>
#include <nano/lib/work.hpp>
#include <nano/node/openclwork.hpp>
#include <nano/secure/common.hpp>
#include <nano/store/component.hpp>
#include <nano/store/lmdb/lmdb.hpp>
#include <nano/store/lmdb/wallet_value.hpp>

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
	kdf (unsigned & kdf_work) :
		kdf_work{ kdf_work }
	{
	}
	void phs (nano::raw_key &, std::string const &, nano::uint256_union const &);
	nano::mutex mutex;
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
	wallet_store (bool &, nano::kdf &, store::transaction &, ::lmdb::env &, nano::account, unsigned, std::string const &);
	wallet_store (bool &, nano::kdf &, store::transaction &, ::lmdb::env &, nano::account, unsigned, std::string const &, std::string const &);
	std::vector<nano::account> accounts (store::transaction const &);
	void initialize (store::transaction const &, std::string const &);
	nano::uint256_union check (store::transaction const &);
	bool rekey (store::transaction const &, std::string const &);
	bool valid_password (store::transaction const &);
	bool valid_public_key (nano::public_key const &);
	bool attempt_password (store::transaction const &, std::string const &);
	void wallet_key (nano::raw_key &, store::transaction const &);
	void seed (nano::raw_key &, store::transaction const &);
	void seed_set (store::transaction const &, nano::raw_key const &);
	nano::key_type key_type (nano::wallet_value const &);
	nano::public_key deterministic_insert (store::transaction const &);
	nano::public_key deterministic_insert (store::transaction const &, uint32_t const);
	nano::raw_key deterministic_key (store::transaction const &, uint32_t);
	uint32_t deterministic_index_get (store::transaction const &);
	void deterministic_index_set (store::transaction const &, uint32_t);
	void deterministic_clear (store::transaction const &);
	nano::uint256_union salt (store::transaction const &);
	bool is_representative (store::transaction const &);
	nano::account representative (store::transaction const &);
	void representative_set (store::transaction const &, nano::account const &);
	nano::public_key insert_adhoc (store::transaction const &, nano::raw_key const &);
	bool insert_watch (store::transaction const &, nano::account const &);
	void erase (store::transaction const &, nano::account const &);
	nano::wallet_value entry_get_raw (store::transaction const &, nano::account const &);
	void entry_put_raw (store::transaction const &, nano::account const &, nano::wallet_value const &);
	bool fetch (store::transaction const &, nano::account const &, nano::raw_key &);
	bool exists (store::transaction const &, nano::account const &);
	void destroy (store::transaction const &);
	store::iterator<nano::account, nano::wallet_value> find (store::transaction const &, nano::account const &);
	store::iterator<nano::account, nano::wallet_value> begin (store::transaction const &, nano::account const &);
	store::iterator<nano::account, nano::wallet_value> begin (store::transaction const &);
	store::iterator<nano::account, nano::wallet_value> end ();
	void derive_key (nano::raw_key &, store::transaction const &, std::string const &);
	void serialize_json (store::transaction const &, std::string &);
	void write_backup (store::transaction const &, std::filesystem::path const &);
	bool move (store::transaction const &, nano::wallet_store &, std::vector<nano::public_key> const &);
	bool import (store::transaction const &, nano::wallet_store &);
	bool work_get (store::transaction const &, nano::public_key const &, uint64_t &);
	void work_put (store::transaction const &, nano::public_key const &, uint64_t);
	unsigned version (store::transaction const &);
	void version_put (store::transaction const &, unsigned);
	nano::fan password;
	nano::fan wallet_key_mem;
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
	nano::kdf & kdf;
	::lmdb::dbi handle;
	std::recursive_mutex mutex;

private:
	::lmdb::env & env;
};

// A wallet is a set of account keys encrypted by a common encryption key
class wallet final : public std::enable_shared_from_this<nano::wallet>
{
public:
	std::shared_ptr<nano::block> change_action (nano::account const &, nano::account const &, uint64_t = 0, bool = true);
	std::shared_ptr<nano::block> receive_action (nano::block_hash const &, nano::account const &, nano::uint128_union const &, nano::account const &, uint64_t = 0, bool = true);
	std::shared_ptr<nano::block> send_action (nano::account const &, nano::account const &, nano::uint128_t const &, uint64_t = 0, bool = true, boost::optional<std::string> = {});
	bool action_complete (std::shared_ptr<nano::block> const &, nano::account const &, bool const, nano::block_details const &);
	wallet (bool &, store::transaction &, nano::wallets &, std::string const &);
	wallet (bool &, store::transaction &, nano::wallets &, std::string const &, std::string const &);
	void enter_initial_password ();
	bool enter_password (store::transaction const &, std::string const &);
	nano::public_key insert_adhoc (nano::raw_key const &, bool = true);
	bool insert_watch (store::transaction const &, nano::public_key const &);
	nano::public_key deterministic_insert (store::transaction const &, bool = true);
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
	void work_update (store::transaction const &, nano::account const &, nano::root const &, uint64_t);
	// Schedule work generation after a few seconds
	void work_ensure (nano::account const &, nano::root const &);
	bool search_receivable (store::transaction const &);
	void init_free_accounts (store::transaction const &);
	uint32_t deterministic_check (store::transaction const & transaction_a, uint32_t index);
	/** Changes the wallet seed and returns the first account */
	nano::public_key change_seed (store::transaction const & transaction_a, nano::raw_key const & prv_a, uint32_t count = 0);
	void deterministic_restore (store::transaction const & transaction_a);
	bool live ();
	std::unordered_set<nano::account> free_accounts;
	std::function<void (bool, bool)> lock_observer;
	nano::wallet_store store;
	nano::wallets & wallets;
	nano::mutex representatives_mutex;
	std::unordered_set<nano::account> representatives;
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

/**
 * The wallets set is all the wallets a node controls.
 * A node may contain multiple wallets independently encrypted and operated.
 */
class wallets final
{
public:
	wallets (bool, nano::node &);
	~wallets ();
	std::shared_ptr<nano::wallet> open (nano::wallet_id const &);
	std::shared_ptr<nano::wallet> create (nano::wallet_id const &);
	bool search_receivable (nano::wallet_id const &);
	void search_receivable_all ();
	void destroy (nano::wallet_id const &);
	void reload ();
	void do_wallet_actions ();
	void queue_wallet_action (nano::uint128_t const &, std::shared_ptr<nano::wallet> const &, std::function<void (nano::wallet &)>);
	void foreach_representative (std::function<void (nano::public_key const &, nano::raw_key const &)> const &);
	bool exists (store::transaction const &, nano::account const &);
	void start ();
	void stop ();
	void clear_send_ids (store::transaction const &);
	nano::wallet_representatives reps () const;
	bool check_rep (nano::account const &, nano::uint128_t const &, bool const = true);
	void compute_reps ();
	void ongoing_compute_reps ();
	void receive_confirmed (nano::block_hash const & hash_a, nano::account const & destination_a);
	std::unordered_map<nano::wallet_id, std::shared_ptr<nano::wallet>> get_wallets ();
	nano::network_params & network_params;
	std::function<void (bool)> observer;
	std::unordered_map<nano::wallet_id, std::shared_ptr<nano::wallet>> items;
	std::multimap<nano::uint128_t, std::pair<std::shared_ptr<nano::wallet>, std::function<void (nano::wallet &)>>, std::greater<nano::uint128_t>> actions;
	nano::locked<std::unordered_map<nano::account, nano::root>> delayed_work;
	nano::mutex mutex;
	nano::mutex action_mutex;
	nano::condition_variable condition;
	nano::kdf kdf;
	::lmdb::dbi handle;
	::lmdb::dbi send_action_ids;
	nano::node & node;
	::lmdb::env & env;
	std::atomic<bool> stopped;
	std::thread thread;
	static nano::uint128_t const generate_priority;
	static nano::uint128_t const high_priority;

	/** Start read-write transaction */
	store::write_transaction tx_begin_write ();
	/** Start read-only transaction */
	store::read_transaction tx_begin_read ();

private:
	mutable nano::mutex reps_cache_mutex;
	nano::wallet_representatives representatives;

	nano::id_t const store_id{ nano::next_id () };
};

std::unique_ptr<container_info_component> collect_container_info (wallets & wallets, std::string const & name);

class wallets_store
{
public:
	virtual ~wallets_store () = default;
	virtual bool init_error () const = 0;
};

class mdb_wallets_store final : public wallets_store
{
public:
	mdb_wallets_store (std::filesystem::path const &, nano::lmdb_config const & lmdb_config_a = nano::lmdb_config{});
	::lmdb::env environment;
	bool init_error () const override;
	bool error{ false };
};
}
