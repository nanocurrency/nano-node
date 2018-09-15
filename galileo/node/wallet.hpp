#pragma once

#include <galileo/node/common.hpp>
#include <galileo/node/lmdb.hpp>
#include <galileo/node/openclwork.hpp>
#include <galileo/secure/blockstore.hpp>
#include <galileo/secure/common.hpp>

#include <mutex>
#include <queue>
#include <thread>
#include <unordered_set>

namespace galileo
{
// The fan spreads a key out over the heap to decrease the likelihood of it being recovered by memory inspection
class fan
{
public:
	fan (galileo::uint256_union const &, size_t);
	void value (galileo::raw_key &);
	void value_set (galileo::raw_key const &);
	std::vector<std::unique_ptr<galileo::uint256_union>> values;

private:
	std::mutex mutex;
	void value_get (galileo::raw_key &);
};
class node_config;
class kdf
{
public:
	void phs (galileo::raw_key &, std::string const &, galileo::uint256_union const &);
	std::mutex mutex;
};
enum class key_type
{
	not_a_type,
	unknown,
	adhoc,
	deterministic
};
class wallet_store
{
public:
	wallet_store (bool &, galileo::kdf &, galileo::transaction &, galileo::account, unsigned, std::string const &);
	wallet_store (bool &, galileo::kdf &, galileo::transaction &, galileo::account, unsigned, std::string const &, std::string const &);
	std::vector<galileo::account> accounts (galileo::transaction const &);
	void initialize (galileo::transaction const &, bool &, std::string const &);
	galileo::uint256_union check (galileo::transaction const &);
	bool rekey (galileo::transaction const &, std::string const &);
	bool valid_password (galileo::transaction const &);
	bool attempt_password (galileo::transaction const &, std::string const &);
	void wallet_key (galileo::raw_key &, galileo::transaction const &);
	void seed (galileo::raw_key &, galileo::transaction const &);
	void seed_set (galileo::transaction const &, galileo::raw_key const &);
	galileo::key_type key_type (galileo::wallet_value const &);
	galileo::public_key deterministic_insert (galileo::transaction const &);
	void deterministic_key (galileo::raw_key &, galileo::transaction const &, uint32_t);
	uint32_t deterministic_index_get (galileo::transaction const &);
	void deterministic_index_set (galileo::transaction const &, uint32_t);
	void deterministic_clear (galileo::transaction const &);
	galileo::uint256_union salt (galileo::transaction const &);
	bool is_representative (galileo::transaction const &);
	galileo::account representative (galileo::transaction const &);
	void representative_set (galileo::transaction const &, galileo::account const &);
	galileo::public_key insert_adhoc (galileo::transaction const &, galileo::raw_key const &);
	void insert_watch (galileo::transaction const &, galileo::public_key const &);
	void erase (galileo::transaction const &, galileo::public_key const &);
	galileo::wallet_value entry_get_raw (galileo::transaction const &, galileo::public_key const &);
	void entry_put_raw (galileo::transaction const &, galileo::public_key const &, galileo::wallet_value const &);
	bool fetch (galileo::transaction const &, galileo::public_key const &, galileo::raw_key &);
	bool exists (galileo::transaction const &, galileo::public_key const &);
	void destroy (galileo::transaction const &);
	galileo::store_iterator<galileo::uint256_union, galileo::wallet_value> find (galileo::transaction const &, galileo::uint256_union const &);
	galileo::store_iterator<galileo::uint256_union, galileo::wallet_value> begin (galileo::transaction const &, galileo::uint256_union const &);
	galileo::store_iterator<galileo::uint256_union, galileo::wallet_value> begin (galileo::transaction const &);
	galileo::store_iterator<galileo::uint256_union, galileo::wallet_value> end ();
	void derive_key (galileo::raw_key &, galileo::transaction const &, std::string const &);
	void serialize_json (galileo::transaction const &, std::string &);
	void write_backup (galileo::transaction const &, boost::filesystem::path const &);
	bool move (galileo::transaction const &, galileo::wallet_store &, std::vector<galileo::public_key> const &);
	bool import (galileo::transaction const &, galileo::wallet_store &);
	bool work_get (galileo::transaction const &, galileo::public_key const &, uint64_t &);
	void work_put (galileo::transaction const &, galileo::public_key const &, uint64_t);
	unsigned version (galileo::transaction const &);
	void version_put (galileo::transaction const &, unsigned);
	void upgrade_v1_v2 (galileo::transaction const &);
	void upgrade_v2_v3 (galileo::transaction const &);
	void upgrade_v3_v4 (galileo::transaction const &);
	galileo::fan password;
	galileo::fan wallet_key_mem;
	static unsigned const version_1 = 1;
	static unsigned const version_2 = 2;
	static unsigned const version_3 = 3;
	static unsigned const version_4 = 4;
	unsigned const version_current = version_4;
	static galileo::uint256_union const version_special;
	static galileo::uint256_union const wallet_key_special;
	static galileo::uint256_union const salt_special;
	static galileo::uint256_union const check_special;
	static galileo::uint256_union const representative_special;
	static galileo::uint256_union const seed_special;
	static galileo::uint256_union const deterministic_index_special;
	static size_t const check_iv_index;
	static size_t const seed_iv_index;
	static int const special_count;
	static unsigned const kdf_full_work = 64 * 1024;
	static unsigned const kdf_test_work = 8;
	static unsigned const kdf_work = galileo::galileo_network == galileo::galileo_networks::galileo_test_network ? kdf_test_work : kdf_full_work;
	galileo::kdf & kdf;
	MDB_dbi handle;
	std::recursive_mutex mutex;

private:
	MDB_txn * tx (galileo::transaction const &) const;
};
class wallets;
// A wallet is a set of account keys encrypted by a common encryption key
class wallet : public std::enable_shared_from_this<galileo::wallet>
{
public:
	std::shared_ptr<galileo::block> change_action (galileo::account const &, galileo::account const &, bool = true);
	std::shared_ptr<galileo::block> receive_action (galileo::block const &, galileo::account const &, galileo::uint128_union const &, bool = true);
	std::shared_ptr<galileo::block> send_action (galileo::account const &, galileo::account const &, galileo::uint128_t const &, bool = true, boost::optional<std::string> = {});
	wallet (bool &, galileo::transaction &, galileo::wallets &, std::string const &);
	wallet (bool &, galileo::transaction &, galileo::wallets &, std::string const &, std::string const &);
	void enter_initial_password ();
	bool enter_password (galileo::transaction const &, std::string const &);
	galileo::public_key insert_adhoc (galileo::raw_key const &, bool = true);
	galileo::public_key insert_adhoc (galileo::transaction const &, galileo::raw_key const &, bool = true);
	void insert_watch (galileo::transaction const &, galileo::public_key const &);
	galileo::public_key deterministic_insert (galileo::transaction const &, bool = true);
	galileo::public_key deterministic_insert (bool = true);
	bool exists (galileo::public_key const &);
	bool import (std::string const &, std::string const &);
	void serialize (std::string &);
	bool change_sync (galileo::account const &, galileo::account const &);
	void change_async (galileo::account const &, galileo::account const &, std::function<void(std::shared_ptr<galileo::block>)> const &, bool = true);
	bool receive_sync (std::shared_ptr<galileo::block>, galileo::account const &, galileo::uint128_t const &);
	void receive_async (std::shared_ptr<galileo::block>, galileo::account const &, galileo::uint128_t const &, std::function<void(std::shared_ptr<galileo::block>)> const &, bool = true);
	galileo::block_hash send_sync (galileo::account const &, galileo::account const &, galileo::uint128_t const &);
	void send_async (galileo::account const &, galileo::account const &, galileo::uint128_t const &, std::function<void(std::shared_ptr<galileo::block>)> const &, bool = true, boost::optional<std::string> = {});
	void work_apply (galileo::account const &, std::function<void(uint64_t)>);
	void work_cache_blocking (galileo::account const &, galileo::block_hash const &);
	void work_update (galileo::transaction const &, galileo::account const &, galileo::block_hash const &, uint64_t);
	void work_ensure (galileo::account const &, galileo::block_hash const &);
	bool search_pending ();
	void init_free_accounts (galileo::transaction const &);
	/** Changes the wallet seed and returns the first account */
	galileo::public_key change_seed (galileo::transaction const & transaction_a, galileo::raw_key const & prv_a);
	std::unordered_set<galileo::account> free_accounts;
	std::function<void(bool, bool)> lock_observer;
	galileo::wallet_store store;
	galileo::wallets & wallets;
};
class node;

/**
 * The wallets set is all the wallets a node controls.
 * A node may contain multiple wallets independently encrypted and operated.
 */
class wallets
{
public:
	wallets (bool &, galileo::node &);
	~wallets ();
	std::shared_ptr<galileo::wallet> open (galileo::uint256_union const &);
	std::shared_ptr<galileo::wallet> create (galileo::uint256_union const &);
	bool search_pending (galileo::uint256_union const &);
	void search_pending_all ();
	void destroy (galileo::uint256_union const &);
	void do_wallet_actions ();
	void queue_wallet_action (galileo::uint128_t const &, std::function<void()> const &);
	void foreach_representative (galileo::transaction const &, std::function<void(galileo::public_key const &, galileo::raw_key const &)> const &);
	bool exists (galileo::transaction const &, galileo::public_key const &);
	void stop ();
	void clear_send_ids (galileo::transaction const &);
	std::function<void(bool)> observer;
	std::unordered_map<galileo::uint256_union, std::shared_ptr<galileo::wallet>> items;
	std::multimap<galileo::uint128_t, std::function<void()>, std::greater<galileo::uint128_t>> actions;
	std::mutex mutex;
	std::condition_variable condition;
	galileo::kdf kdf;
	MDB_dbi handle;
	MDB_dbi send_action_ids;
	galileo::node & node;
	galileo::mdb_env & env;
	bool stopped;
	std::thread thread;
	static galileo::uint128_t const generate_priority;
	static galileo::uint128_t const high_priority;

	/** Start read-write transaction */
	galileo::transaction tx_begin_write ();

	/** Start read-only transaction */
	galileo::transaction tx_begin_read ();

	/**
	 * Start a read-only or read-write transaction
	 * @param write If true, start a read-write transaction
	 */
	galileo::transaction tx_begin (bool write = false);
};
}
