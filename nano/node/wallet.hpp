#pragma once

#include <nano/lib/config.hpp>
#include <nano/node/lmdb/lmdb.hpp>
#include <nano/node/lmdb/wallet_value.hpp>
#include <nano/node/openclwork.hpp>
#include <nano/secure/blockstore.hpp>
#include <nano/secure/common.hpp>

#include <boost/thread/thread.hpp>

#include <mutex>
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
	fan (nano::uint256_union const &, size_t);
	void value (nano::raw_key &);
	void value_set (nano::raw_key const &);
	std::vector<std::unique_ptr<nano::uint256_union>> values;

private:
	std::mutex mutex;
	void value_get (nano::raw_key &);
};
class kdf final
{
public:
	void phs (nano::raw_key &, std::string const &, nano::uint256_union const &);
	std::mutex mutex;
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
	wallet_store (bool &, nano::kdf &, nano::transaction &, nano::account, unsigned, std::string const &);
	wallet_store (bool &, nano::kdf &, nano::transaction &, nano::account, unsigned, std::string const &, std::string const &);
	std::vector<nano::account> accounts (nano::transaction const &);
	void initialize (nano::transaction const &, bool &, std::string const &);
	nano::uint256_union check (nano::transaction const &);
	bool rekey (nano::transaction const &, std::string const &);
	bool valid_password (nano::transaction const &);
	bool valid_public_key (nano::public_key const &);
	bool attempt_password (nano::transaction const &, std::string const &);
	void wallet_key (nano::raw_key &, nano::transaction const &);
	void seed (nano::raw_key &, nano::transaction const &);
	void seed_set (nano::transaction const &, nano::raw_key const &);
	nano::key_type key_type (nano::wallet_value const &);
	nano::public_key deterministic_insert (nano::transaction const &);
	nano::public_key deterministic_insert (nano::transaction const &, uint32_t const);
	void deterministic_key (nano::raw_key &, nano::transaction const &, uint32_t);
	uint32_t deterministic_index_get (nano::transaction const &);
	void deterministic_index_set (nano::transaction const &, uint32_t);
	void deterministic_clear (nano::transaction const &);
	nano::uint256_union salt (nano::transaction const &);
	bool is_representative (nano::transaction const &);
	nano::account representative (nano::transaction const &);
	void representative_set (nano::transaction const &, nano::account const &);
	nano::public_key insert_adhoc (nano::transaction const &, nano::raw_key const &);
	bool insert_watch (nano::transaction const &, nano::public_key const &);
	void erase (nano::transaction const &, nano::public_key const &);
	nano::wallet_value entry_get_raw (nano::transaction const &, nano::public_key const &);
	void entry_put_raw (nano::transaction const &, nano::public_key const &, nano::wallet_value const &);
	bool fetch (nano::transaction const &, nano::public_key const &, nano::raw_key &);
	bool exists (nano::transaction const &, nano::public_key const &);
	void destroy (nano::transaction const &);
	nano::store_iterator<nano::uint256_union, nano::wallet_value> find (nano::transaction const &, nano::uint256_union const &);
	nano::store_iterator<nano::uint256_union, nano::wallet_value> begin (nano::transaction const &, nano::uint256_union const &);
	nano::store_iterator<nano::uint256_union, nano::wallet_value> begin (nano::transaction const &);
	nano::store_iterator<nano::uint256_union, nano::wallet_value> end ();
	void derive_key (nano::raw_key &, nano::transaction const &, std::string const &);
	void serialize_json (nano::transaction const &, std::string &);
	void write_backup (nano::transaction const &, boost::filesystem::path const &);
	bool move (nano::transaction const &, nano::wallet_store &, std::vector<nano::public_key> const &);
	bool import (nano::transaction const &, nano::wallet_store &);
	bool work_get (nano::transaction const &, nano::public_key const &, uint64_t &);
	void work_put (nano::transaction const &, nano::public_key const &, uint64_t);
	unsigned version (nano::transaction const &);
	void version_put (nano::transaction const &, unsigned);
	void upgrade_v1_v2 (nano::transaction const &);
	void upgrade_v2_v3 (nano::transaction const &);
	void upgrade_v3_v4 (nano::transaction const &);
	nano::fan password;
	nano::fan wallet_key_mem;
	static unsigned const version_1 = 1;
	static unsigned const version_2 = 2;
	static unsigned const version_3 = 3;
	static unsigned const version_4 = 4;
	static unsigned constexpr version_current = version_4;
	static nano::uint256_union const version_special;
	static nano::uint256_union const wallet_key_special;
	static nano::uint256_union const salt_special;
	static nano::uint256_union const check_special;
	static nano::uint256_union const representative_special;
	static nano::uint256_union const seed_special;
	static nano::uint256_union const deterministic_index_special;
	static size_t const check_iv_index;
	static size_t const seed_iv_index;
	static int const special_count;
	nano::kdf & kdf;
	MDB_dbi handle;
	std::recursive_mutex mutex;

private:
	MDB_txn * tx (nano::transaction const &) const;
};
// A wallet is a set of account keys encrypted by a common encryption key
class wallet final : public std::enable_shared_from_this<nano::wallet>
{
public:
	std::shared_ptr<nano::block> change_action (nano::account const &, nano::account const &, uint64_t = 0, bool = true);
	std::shared_ptr<nano::block> receive_action (nano::block const &, nano::account const &, nano::uint128_union const &, uint64_t = 0, bool = true);
	std::shared_ptr<nano::block> send_action (nano::account const &, nano::account const &, nano::uint128_t const &, uint64_t = 0, bool = true, boost::optional<std::string> = {});
	bool action_complete (std::shared_ptr<nano::block> const &, nano::account const &, bool const);
	wallet (bool &, nano::transaction &, nano::wallets &, std::string const &);
	wallet (bool &, nano::transaction &, nano::wallets &, std::string const &, std::string const &);
	void enter_initial_password ();
	bool enter_password (nano::transaction const &, std::string const &);
	nano::public_key insert_adhoc (nano::raw_key const &, bool = true);
	nano::public_key insert_adhoc (nano::transaction const &, nano::raw_key const &, bool = true);
	bool insert_watch (nano::transaction const &, nano::public_key const &);
	nano::public_key deterministic_insert (nano::transaction const &, bool = true);
	nano::public_key deterministic_insert (uint32_t, bool = true);
	nano::public_key deterministic_insert (bool = true);
	bool exists (nano::public_key const &);
	bool import (std::string const &, std::string const &);
	void serialize (std::string &);
	bool change_sync (nano::account const &, nano::account const &);
	void change_async (nano::account const &, nano::account const &, std::function<void(std::shared_ptr<nano::block>)> const &, uint64_t = 0, bool = true);
	bool receive_sync (std::shared_ptr<nano::block>, nano::account const &, nano::uint128_t const &);
	void receive_async (std::shared_ptr<nano::block>, nano::account const &, nano::uint128_t const &, std::function<void(std::shared_ptr<nano::block>)> const &, uint64_t = 0, bool = true);
	nano::block_hash send_sync (nano::account const &, nano::account const &, nano::uint128_t const &);
	void send_async (nano::account const &, nano::account const &, nano::uint128_t const &, std::function<void(std::shared_ptr<nano::block>)> const &, uint64_t = 0, bool = true, boost::optional<std::string> = {});
	void work_cache_blocking (nano::account const &, nano::block_hash const &);
	void work_update (nano::transaction const &, nano::account const &, nano::block_hash const &, uint64_t);
	void work_ensure (nano::account const &, nano::block_hash const &);
	bool search_pending ();
	void init_free_accounts (nano::transaction const &);
	uint32_t deterministic_check (nano::transaction const & transaction_a, uint32_t index);
	/** Changes the wallet seed and returns the first account */
	nano::public_key change_seed (nano::transaction const & transaction_a, nano::raw_key const & prv_a, uint32_t count = 0);
	void deterministic_restore (nano::transaction const & transaction_a);
	bool live ();
	nano::network_params network_params;
	std::unordered_set<nano::account> free_accounts;
	std::function<void(bool, bool)> lock_observer;
	nano::wallet_store store;
	nano::wallets & wallets;
	std::mutex representatives_mutex;
	std::unordered_set<nano::account> representatives;
};

class work_watcher final : public std::enable_shared_from_this<nano::work_watcher>
{
public:
	work_watcher (nano::node &);
	~work_watcher ();
	void stop ();
	void add (std::shared_ptr<nano::block>);
	void update (nano::qualified_root const &, std::shared_ptr<nano::state_block>);
	void watching (nano::qualified_root const &, std::shared_ptr<nano::state_block>);
	void remove (std::shared_ptr<nano::block>);
	bool is_watched (nano::qualified_root const &);
	size_t size ();
	std::mutex mutex;
	nano::node & node;
	std::unordered_map<nano::qualified_root, std::shared_ptr<nano::state_block>> watched;
	std::atomic<bool> stopped;
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
	std::shared_ptr<nano::wallet> open (nano::uint256_union const &);
	std::shared_ptr<nano::wallet> create (nano::uint256_union const &);
	bool search_pending (nano::uint256_union const &);
	void search_pending_all ();
	void destroy (nano::uint256_union const &);
	void reload ();
	void do_wallet_actions ();
	void queue_wallet_action (nano::uint128_t const &, std::shared_ptr<nano::wallet>, std::function<void(nano::wallet &)> const &);
	void foreach_representative (std::function<void(nano::public_key const &, nano::raw_key const &)> const &);
	bool exists (nano::transaction const &, nano::public_key const &);
	void stop ();
	void clear_send_ids (nano::transaction const &);
	bool check_rep (nano::account const &, nano::uint128_t const &);
	void compute_reps ();
	void ongoing_compute_reps ();
	void split_if_needed (nano::transaction &, nano::block_store &);
	void move_table (std::string const &, MDB_txn *, MDB_txn *);
	nano::network_params network_params;
	std::function<void(bool)> observer;
	std::unordered_map<nano::uint256_union, std::shared_ptr<nano::wallet>> items;
	std::multimap<nano::uint128_t, std::pair<std::shared_ptr<nano::wallet>, std::function<void(nano::wallet &)>>, std::greater<nano::uint128_t>> actions;
	std::mutex mutex;
	std::mutex action_mutex;
	nano::condition_variable condition;
	nano::kdf kdf;
	MDB_dbi handle;
	MDB_dbi send_action_ids;
	nano::node & node;
	nano::mdb_env & env;
	std::atomic<bool> stopped;
	std::shared_ptr<nano::work_watcher> watcher;
	boost::thread thread;
	static nano::uint128_t const generate_priority;
	static nano::uint128_t const high_priority;
	std::atomic<uint64_t> reps_count{ 0 };
	std::atomic<uint64_t> half_principal_reps_count{ 0 }; // Representatives with at least 50% of principal representative requirements

	/** Start read-write transaction */
	nano::write_transaction tx_begin_write ();

	/** Start read-only transaction */
	nano::read_transaction tx_begin_read ();
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (wallets & wallets, const std::string & name);

class wallets_store
{
public:
	virtual ~wallets_store () = default;
	virtual bool init_error () const = 0;
};
class mdb_wallets_store final : public wallets_store
{
public:
	mdb_wallets_store (boost::filesystem::path const &, int lmdb_max_dbs = 128);
	nano::mdb_env environment;
	bool init_error () const override;
	bool error{ false };
};
}
