#pragma once

#include <rai/blockstore.hpp>
#include <rai/common.hpp>
#include <rai/node/common.hpp>
#include <rai/node/openclwork.hpp>

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
class wallet_value
{
public:
	wallet_value () = default;
	wallet_value (rai::mdb_val const &);
	wallet_value (rai::uint256_union const &, uint64_t);
	rai::mdb_val val () const;
	rai::private_key key;
	uint64_t work;
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
	std::vector<rai::account> accounts (MDB_txn *);
	void initialize (MDB_txn *, bool &, std::string const &);
	rai::uint256_union check (MDB_txn *);
	bool rekey (MDB_txn *, std::string const &);
	bool valid_password (MDB_txn *);
	bool attempt_password (MDB_txn *, std::string const &);
	void wallet_key (rai::raw_key &, MDB_txn *);
	void seed (rai::raw_key &, MDB_txn *);
	void seed_set (MDB_txn *, rai::raw_key const &);
	rai::key_type key_type (rai::wallet_value const &);
	rai::public_key deterministic_insert (MDB_txn *);
	void deterministic_key (rai::raw_key &, MDB_txn *, uint32_t);
	uint32_t deterministic_index_get (MDB_txn *);
	void deterministic_index_set (MDB_txn *, uint32_t);
	void deterministic_clear (MDB_txn *);
	rai::uint256_union salt (MDB_txn *);
	bool is_representative (MDB_txn *);
	rai::account representative (MDB_txn *);
	void representative_set (MDB_txn *, rai::account const &);
	rai::public_key insert_adhoc (MDB_txn *, rai::raw_key const &);
	void erase (MDB_txn *, rai::public_key const &);
	rai::wallet_value entry_get_raw (MDB_txn *, rai::public_key const &);
	void entry_put_raw (MDB_txn *, rai::public_key const &, rai::wallet_value const &);
	bool fetch (MDB_txn *, rai::public_key const &, rai::raw_key &);
	bool exists (MDB_txn *, rai::public_key const &);
	void destroy (MDB_txn *);
	rai::store_iterator find (MDB_txn *, rai::uint256_union const &);
	rai::store_iterator begin (MDB_txn *, rai::uint256_union const &);
	rai::store_iterator begin (MDB_txn *);
	rai::store_iterator end ();
	void derive_key (rai::raw_key &, MDB_txn *, std::string const &);
	void serialize_json (MDB_txn *, std::string &);
	void write_backup (MDB_txn *, boost::filesystem::path const &);
	bool move (MDB_txn *, rai::wallet_store &, std::vector<rai::public_key> const &);
	bool import (MDB_txn *, rai::wallet_store &);
	bool work_get (MDB_txn *, rai::public_key const &, uint64_t &);
	void work_put (MDB_txn *, rai::public_key const &, uint64_t);
	unsigned version (MDB_txn *);
	void version_put (MDB_txn *, unsigned);
	void upgrade_v1_v2 ();
	void upgrade_v2_v3 ();
	rai::fan password;
	rai::fan wallet_key_mem;
	static unsigned const version_1;
	static unsigned const version_2;
	static unsigned const version_3;
	static unsigned const version_current;
	static rai::uint256_union const version_special;
	static rai::uint256_union const wallet_key_special;
	static rai::uint256_union const salt_special;
	static rai::uint256_union const check_special;
	static rai::uint256_union const representative_special;
	static rai::uint256_union const seed_special;
	static rai::uint256_union const deterministic_index_special;
	static int const special_count;
	static unsigned const kdf_full_work = 64 * 1024;
	static unsigned const kdf_test_work = 8;
	static unsigned const kdf_work = rai::rai_network == rai::rai_networks::rai_test_network ? kdf_test_work : kdf_full_work;
	rai::kdf & kdf;
	rai::mdb_env & environment;
	MDB_dbi handle;
	std::recursive_mutex mutex;
};
class node;
// A wallet is a set of account keys encrypted by a common encryption key
class wallet : public std::enable_shared_from_this<rai::wallet>
{
public:
	std::shared_ptr<rai::block> change_action (rai::account const &, rai::account const &, bool = true);
	std::shared_ptr<rai::block> receive_action (rai::block const &, rai::account const &, rai::uint128_union const &, bool = true);
	std::shared_ptr<rai::block> send_action (rai::account const &, rai::account const &, rai::uint128_t const &, bool = true, boost::optional<std::string> = {});
	wallet (bool &, rai::transaction &, rai::node &, std::string const &);
	wallet (bool &, rai::transaction &, rai::node &, std::string const &, std::string const &);
	void enter_initial_password ();
	bool valid_password ();
	bool enter_password (std::string const &);
	rai::public_key insert_adhoc (rai::raw_key const &, bool = true);
	rai::public_key insert_adhoc (MDB_txn *, rai::raw_key const &, bool = true);
	rai::public_key deterministic_insert (MDB_txn *, bool = true);
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
	void work_generate (rai::account const &, rai::block_hash const &);
	void work_update (MDB_txn *, rai::account const &, rai::block_hash const &, uint64_t);
	uint64_t work_fetch (MDB_txn *, rai::account const &, rai::block_hash const &);
	void work_ensure (MDB_txn *, rai::account const &);
	bool search_pending ();
	void init_free_accounts (MDB_txn *);
	/** Changes the wallet seed and returns the first account */
	rai::public_key change_seed (MDB_txn * transaction_a, rai::raw_key const & prv_a);
	std::unordered_set<rai::account> free_accounts;
	std::function<void(bool, bool)> lock_observer;
	rai::wallet_store store;
	rai::node & node;
};
// The wallets set is all the wallets a node controls.  A node may contain multiple wallets independently encrypted and operated.
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
	void foreach_representative (MDB_txn *, std::function<void(rai::public_key const &, rai::raw_key const &)> const &);
	bool exists (MDB_txn *, rai::public_key const &);
	void stop ();
	std::function<void(bool)> observer;
	std::unordered_map<rai::uint256_union, std::shared_ptr<rai::wallet>> items;
	std::multimap<rai::uint128_t, std::function<void()>, std::greater<rai::uint128_t>> actions;
	std::mutex mutex;
	std::condition_variable condition;
	rai::kdf kdf;
	MDB_dbi handle;
	MDB_dbi send_action_ids;
	rai::node & node;
	bool stopped;
	std::thread thread;
	static rai::uint128_t const generate_priority;
	static rai::uint128_t const high_priority;
};
}
