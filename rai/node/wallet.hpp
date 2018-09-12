#pragma once

#include <rai/node/common.hpp>
#include <rai/node/lmdb.hpp>
#include <rai/node/openclwork.hpp>
#include <rai/secure/blockstore.hpp>
#include <rai/secure/common.hpp>

#include <mutex>
#include <queue>
#include <thread>
#include <unordered_set>

namespace rai
{
// The fan spreads a key out over the heap to decrease the likelihood of it being recovered by memory inspection
class fan
{
public:
	fan (rai::uint256_union const &, size_t);
	void value (rai::raw_key &);
	void value_set (rai::raw_key const &);
	std::vector<std::unique_ptr<rai::uint256_union>> values;

private:
	std::mutex mutex;
	void value_get (rai::raw_key &);
};
class node_config;
class kdf
{
public:
	void phs (rai::raw_key &, std::string const &, rai::uint256_union const &);
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
	wallet_store (bool &, rai::kdf &, rai::transaction &, rai::account, unsigned, std::string const &);
	wallet_store (bool &, rai::kdf &, rai::transaction &, rai::account, unsigned, std::string const &, std::string const &);
	std::vector<rai::account> accounts (rai::transaction const &);
	void initialize (rai::transaction const &, bool &, std::string const &);
	rai::uint256_union check (rai::transaction const &);
	bool rekey (rai::transaction const &, std::string const &);
	bool valid_password (rai::transaction const &);
	bool attempt_password (rai::transaction const &, std::string const &);
	void wallet_key (rai::raw_key &, rai::transaction const &);
	void seed (rai::raw_key &, rai::transaction const &);
	void seed_set (rai::transaction const &, rai::raw_key const &);
	rai::key_type key_type (rai::wallet_value const &);
	rai::public_key deterministic_insert (rai::transaction const &);
	void deterministic_key (rai::raw_key &, rai::transaction const &, uint32_t);
	uint32_t deterministic_index_get (rai::transaction const &);
	void deterministic_index_set (rai::transaction const &, uint32_t);
	void deterministic_clear (rai::transaction const &);
	rai::uint256_union salt (rai::transaction const &);
	bool is_representative (rai::transaction const &);
	rai::account representative (rai::transaction const &);
	void representative_set (rai::transaction const &, rai::account const &);
	rai::public_key insert_adhoc (rai::transaction const &, rai::raw_key const &);
	void insert_watch (rai::transaction const &, rai::public_key const &);
	void erase (rai::transaction const &, rai::public_key const &);
	rai::wallet_value entry_get_raw (rai::transaction const &, rai::public_key const &);
	void entry_put_raw (rai::transaction const &, rai::public_key const &, rai::wallet_value const &);
	bool fetch (rai::transaction const &, rai::public_key const &, rai::raw_key &);
	bool exists (rai::transaction const &, rai::public_key const &);
	void destroy (rai::transaction const &);
	rai::store_iterator<rai::uint256_union, rai::wallet_value> find (rai::transaction const &, rai::uint256_union const &);
	rai::store_iterator<rai::uint256_union, rai::wallet_value> begin (rai::transaction const &, rai::uint256_union const &);
	rai::store_iterator<rai::uint256_union, rai::wallet_value> begin (rai::transaction const &);
	rai::store_iterator<rai::uint256_union, rai::wallet_value> end ();
	void derive_key (rai::raw_key &, rai::transaction const &, std::string const &);
	void serialize_json (rai::transaction const &, std::string &);
	void write_backup (rai::transaction const &, boost::filesystem::path const &);
	bool move (rai::transaction const &, rai::wallet_store &, std::vector<rai::public_key> const &);
	bool import (rai::transaction const &, rai::wallet_store &);
	bool work_get (rai::transaction const &, rai::public_key const &, uint64_t &);
	void work_put (rai::transaction const &, rai::public_key const &, uint64_t);
	unsigned version (rai::transaction const &);
	void version_put (rai::transaction const &, unsigned);
	void upgrade_v1_v2 (rai::transaction const &);
	void upgrade_v2_v3 (rai::transaction const &);
	void upgrade_v3_v4 (rai::transaction const &);
	rai::fan password;
	rai::fan wallet_key_mem;
	static unsigned const version_1 = 1;
	static unsigned const version_2 = 2;
	static unsigned const version_3 = 3;
	static unsigned const version_4 = 4;
	unsigned const version_current = version_4;
	static rai::uint256_union const version_special;
	static rai::uint256_union const wallet_key_special;
	static rai::uint256_union const salt_special;
	static rai::uint256_union const check_special;
	static rai::uint256_union const representative_special;
	static rai::uint256_union const seed_special;
	static rai::uint256_union const deterministic_index_special;
	static size_t const check_iv_index;
	static size_t const seed_iv_index;
	static int const special_count;
	static unsigned const kdf_full_work = 64 * 1024;
	static unsigned const kdf_test_work = 8;
	static unsigned const kdf_work = rai::rai_network == rai::rai_networks::rai_test_network ? kdf_test_work : kdf_full_work;
	rai::kdf & kdf;
	MDB_dbi handle;
	std::recursive_mutex mutex;

private:
	MDB_txn * tx (rai::transaction const &) const;
};
class wallets;
// A wallet is a set of account keys encrypted by a common encryption key
class wallet : public std::enable_shared_from_this<rai::wallet>
{
public:
	std::shared_ptr<rai::block> change_action (rai::account const &, rai::account const &, bool = true);
	std::shared_ptr<rai::block> receive_action (rai::block const &, rai::account const &, rai::uint128_union const &, bool = true);
	std::shared_ptr<rai::block> send_action (rai::account const &, rai::account const &, rai::uint128_t const &, bool = true, boost::optional<std::string> = {});
	wallet (bool &, rai::transaction &, rai::wallets &, std::string const &);
	wallet (bool &, rai::transaction &, rai::wallets &, std::string const &, std::string const &);
	void enter_initial_password ();
	bool enter_password (rai::transaction const &, std::string const &);
	rai::public_key insert_adhoc (rai::raw_key const &, bool = true);
	rai::public_key insert_adhoc (rai::transaction const &, rai::raw_key const &, bool = true);
	void insert_watch (rai::transaction const &, rai::public_key const &);
	rai::public_key deterministic_insert (rai::transaction const &, bool = true);
	rai::public_key deterministic_insert (bool = true);
	bool exists (rai::public_key const &);
	bool import (std::string const &, std::string const &);
	void serialize (std::string &);
	bool change_sync (rai::account const &, rai::account const &);
	void change_async (rai::account const &, rai::account const &, std::function<void(std::shared_ptr<rai::block>)> const &, bool = true);
	bool receive_sync (std::shared_ptr<rai::block>, rai::account const &, rai::uint128_t const &);
	void receive_async (std::shared_ptr<rai::block>, rai::account const &, rai::uint128_t const &, std::function<void(std::shared_ptr<rai::block>)> const &, bool = true);
	rai::block_hash send_sync (rai::account const &, rai::account const &, rai::uint128_t const &);
	void send_async (rai::account const &, rai::account const &, rai::uint128_t const &, std::function<void(std::shared_ptr<rai::block>)> const &, bool = true, boost::optional<std::string> = {});
	void work_apply (rai::account const &, std::function<void(uint64_t)>);
	void work_cache_blocking (rai::account const &, rai::block_hash const &);
	void work_update (rai::transaction const &, rai::account const &, rai::block_hash const &, uint64_t);
	void work_ensure (rai::account const &, rai::block_hash const &);
	bool search_pending ();
	void init_free_accounts (rai::transaction const &);
	/** Changes the wallet seed and returns the first account */
	rai::public_key change_seed (rai::transaction const & transaction_a, rai::raw_key const & prv_a);
	std::unordered_set<rai::account> free_accounts;
	std::function<void(bool, bool)> lock_observer;
	rai::wallet_store store;
	rai::wallets & wallets;
};
class node;

/**
 * The wallets set is all the wallets a node controls.
 * A node may contain multiple wallets independently encrypted and operated.
 */
class wallets
{
public:
	wallets (bool &, rai::node &);
	~wallets ();
	std::shared_ptr<rai::wallet> open (rai::uint256_union const &);
	std::shared_ptr<rai::wallet> create (rai::uint256_union const &);
	bool search_pending (rai::uint256_union const &);
	void search_pending_all ();
	void destroy (rai::uint256_union const &);
	void do_wallet_actions ();
	void queue_wallet_action (rai::uint128_t const &, std::function<void()> const &);
	void foreach_representative (rai::transaction const &, std::function<void(rai::public_key const &, rai::raw_key const &)> const &);
	bool exists (rai::transaction const &, rai::public_key const &);
	void stop ();
	void clear_send_ids (rai::transaction const &);
	std::function<void(bool)> observer;
	std::unordered_map<rai::uint256_union, std::shared_ptr<rai::wallet>> items;
	std::multimap<rai::uint128_t, std::function<void()>, std::greater<rai::uint128_t>> actions;
	std::mutex mutex;
	std::condition_variable condition;
	rai::kdf kdf;
	MDB_dbi handle;
	MDB_dbi send_action_ids;
	rai::node & node;
	rai::mdb_env & env;
	bool stopped;
	std::thread thread;
	static rai::uint128_t const generate_priority;
	static rai::uint128_t const high_priority;

	/** Start read-write transaction */
	rai::transaction tx_begin_write ();

	/** Start read-only transaction */
	rai::transaction tx_begin_read ();

	/**
	 * Start a read-only or read-write transaction
	 * @param write If true, start a read-write transaction
	 */
	rai::transaction tx_begin (bool write = false);
};
}
