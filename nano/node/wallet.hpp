#pragma once

#include <nano/lib/fan.hpp>
#include <nano/lib/kdf.hpp>
#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/numbers_templ.hpp>
#include <nano/lib/result.hpp>
#include <nano/lib/thread_pool.hpp>
#include <nano/lib/work.hpp>
#include <nano/node/fwd.hpp>
#include <nano/node/openclwork.hpp>
#include <nano/secure/common.hpp>
#include <nano/store/typed_iterator.hpp>
#include <nano/wallet/wallet_value.hpp>
#include <nano/wallet/wallets_backend.hpp>

#include <mutex>
#include <optional>
#include <thread>
#include <unordered_set>

namespace nano::wallet
{
enum class key_type
{
	not_a_type,
	unknown,
	adhoc,
	deterministic
};

class wallet_store;

/**
 * Validated handle for wallet-key crypto. The only way to obtain one is via wallet_store::unlock(), which has therefore already validated the password.
 * The cipher exposes encrypt/decrypt but never the underlying wallet key, so it is impossible to encrypt or decrypt with an unvalidated key.
 */
class wallet_cipher final
{
public:
	nano::raw_key encrypt (nano::raw_key const & plaintext, nano::uint128_union const & iv) const;
	nano::raw_key decrypt (nano::uint256_union const & ciphertext, nano::uint128_union const & iv) const;

	// Re-encrypt the wallet key under a new password key. Used by rekey.
	nano::raw_key reseal (nano::raw_key const & new_password_key, nano::uint128_union const & iv) const;

private:
	friend class wallet_store;
	explicit wallet_cipher (nano::raw_key wallet_key);

	nano::raw_key wallet_key;
};

class wallet_store final
{
public:
	using iterator = store::typed_iterator<nano::account, nano::wallet::wallet_value>;

public:
	wallet_store (nano::kdf &, nano::store::write_transaction &, nano::wallet::wallets_backend &, nano::account representative, unsigned fanout, std::string const & wallet_path);
	wallet_store (nano::kdf &, nano::store::write_transaction &, nano::wallet::wallets_backend &, unsigned fanout, std::string const & wallet_path, std::string const & json);

	// Returns a cipher if the password decrypts the wallet key correctly.
	// The cipher is the only way to encrypt/decrypt account data.
	std::optional<nano::wallet::wallet_cipher> unlock (nano::store::transaction const &) const;

	std::vector<nano::account> accounts (nano::store::transaction const &) const;
	bool rekey (nano::store::write_transaction const &, std::string const & password);
	bool valid_password (nano::store::transaction const &) const;
	bool valid_public_key (nano::public_key const &) const;
	bool attempt_password (nano::store::transaction const &, std::string const & password);
	nano::raw_key seed (nano::store::transaction const &) const;
	void seed_set (nano::store::write_transaction const &, nano::raw_key const & seed);
	nano::wallet::key_type key_type (nano::wallet::wallet_value const &) const;
	nano::public_key deterministic_insert (nano::store::write_transaction const &);
	nano::public_key deterministic_insert (nano::store::write_transaction const &, uint32_t index);
	nano::raw_key deterministic_key (nano::store::transaction const &, uint32_t index) const;
	uint32_t deterministic_index_get (nano::store::transaction const &) const;
	void deterministic_index_set (nano::store::write_transaction const &, uint32_t index);
	void deterministic_clear (nano::store::write_transaction const &);
	nano::uint256_union salt_get (nano::store::transaction const &) const;
	nano::uint256_union check_value_get (nano::store::transaction const &) const;
	bool is_representative (nano::store::transaction const &) const;
	nano::account representative (nano::store::transaction const &) const;
	void representative_set (nano::store::write_transaction const &, nano::account const & rep);
	nano::public_key insert_adhoc (nano::store::write_transaction const &, nano::raw_key const & prv);
	bool insert_watch (nano::store::write_transaction const &, nano::account const & pub);
	void erase (nano::store::write_transaction const &, nano::account const & pub);
	nano::wallet::wallet_value entry_get_raw (nano::store::transaction const &, nano::account const & pub) const;
	void entry_put_raw (nano::store::write_transaction const &, nano::account const & pub, nano::wallet::wallet_value const & entry);
	nano::result<nano::raw_key> fetch (nano::store::transaction const &, nano::account const & pub) const;
	bool exists (nano::store::transaction const &, nano::account const & pub) const;
	void destroy (nano::store::write_transaction const &);
	iterator find (nano::store::transaction const &, nano::account const & key) const;
	iterator begin (nano::store::transaction const &, nano::account const & key) const;
	iterator begin (nano::store::transaction const &) const;
	iterator end (nano::store::transaction const &) const;
	nano::raw_key derive_key (nano::store::transaction const &, std::string const & password) const;
	void serialize_json (nano::store::transaction const &, std::string & json) const;
	void write_backup (nano::store::transaction const &, std::filesystem::path const & path) const;
	nano::result<bool> move (nano::store::write_transaction const &, wallet_store & source, std::vector<nano::public_key> const & keys);
	nano::result<bool> import (nano::store::write_transaction const &, wallet_store & source);
	std::optional<uint64_t> work_get (nano::store::transaction const &, nano::public_key const &) const;
	void work_put (nano::store::write_transaction const &, nano::public_key const & pub, uint64_t work);
	unsigned version (nano::store::transaction const &) const;
	void version_put (nano::store::write_transaction const &, unsigned version);

public:
	nano::fan password;
	nano::fan wallet_key_mem;
	nano::kdf & kdf;
	nano::locked<nano::wallet::wallet_handle> handle;
	mutable std::recursive_mutex mutex;

private:
	// Decrypts the wallet key using whatever the live password fan currently is.
	// Used only by unlock() and during construction; callers outside wallet_store
	// must go through unlock() so that the password is validated first.
	nano::raw_key wallet_key_decrypt (nano::store::transaction const &) const;

	// Decrypts the wallet seed using an already-unlocked cipher.
	// Centralises the seed_special / seed_iv_index plumbing so callers can't drift.
	nano::raw_key seed_decrypt (nano::store::transaction const &, nano::wallet::wallet_cipher const &) const;

private:
	nano::wallet::wallets_backend & backend;

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
	static std::string const default_password;
};

/**
 * A wallet is a set of account keys encrypted by a common encryption key
 */
class wallet final : public std::enable_shared_from_this<wallet>
{
public:
	wallet (nano::store::write_transaction &, wallets &, std::string const & wallet_path);
	wallet (nano::store::write_transaction &, wallets &, std::string const & wallet_path, std::string const & json);

	// Password and lock management
	void enter_initial_password ();
	bool enter_password (std::string const & password);
	bool rekey (std::string const & password);
	bool is_locked () const;
	void lock ();

	// Account management
	nano::result<nano::public_key> insert_adhoc (nano::raw_key const & prv, bool generate_work = true);
	nano::result<nano::public_key> deterministic_insert (uint32_t index, bool generate_work = true);
	nano::result<nano::public_key> deterministic_insert (bool generate_work = true);
	bool insert_watch (nano::public_key const & pub);
	void remove_account (nano::account const & account);
	std::vector<nano::account> accounts () const;
	bool exists (nano::public_key const & pub);
	nano::result<bool> move_accounts (wallet & source, std::vector<nano::public_key> const & accounts);
	nano::wallet::key_type key_type (nano::account const & account) const;

	// Seed management
	nano::result<nano::raw_key> get_seed () const;
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
	nano::result<nano::raw_key> fetch_prv (nano::account const & pub) const;

	// Block actions
	std::shared_ptr<nano::block> change_action (nano::account const & source, nano::account const & representative, uint64_t work = 0, bool generate_work = true);
	std::shared_ptr<nano::block> receive_action (nano::block_hash const & send_hash, nano::account const & representative, nano::uint128_union const & amount, nano::account const & account, uint64_t work = 0, bool generate_work = true);
	std::shared_ptr<nano::block> send_action (nano::account const & source, nano::account const & destination, nano::uint128_t const & amount, uint64_t work = 0, bool generate_work = true, std::optional<std::string> id = {});
	bool action_complete (std::shared_ptr<nano::block> const & block, nano::account const & account, bool generate_work, nano::block_details const & details);

	// Sync/async block operations
	bool change_sync (nano::account const & source, nano::account const & representative);
	void change_async (nano::account const & source, nano::account const & representative, std::function<void (std::shared_ptr<nano::block> const &)> const & action, uint64_t work = 0, bool generate_work = true);
	bool receive_sync (std::shared_ptr<nano::block> const & block, nano::account const & representative, nano::uint128_t const & amount);
	void receive_async (nano::block_hash const & hash, nano::account const & representative, nano::uint128_t const & amount, nano::account const & account, std::function<void (std::shared_ptr<nano::block> const &)> const & action, uint64_t work = 0, bool generate_work = true);
	nano::block_hash send_sync (nano::account const & source, nano::account const & destination, nano::uint128_t const & amount);
	void send_async (nano::account const & source, nano::account const & destination, nano::uint128_t const & amount, std::function<void (std::shared_ptr<nano::block> const &)> const & action, uint64_t work = 0, bool generate_work = true, std::optional<std::string> id = {});

	// Work cache
	void work_cache_blocking (nano::account const & account, nano::root const & root);
	void work_ensure (nano::account const & account, nano::root const & root);
	nano::result<uint64_t> get_work (nano::public_key const &) const;
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
	nano::wallet::wallet_store store;
	nano::wallet::wallets & wallets;
	nano::logger & logger;

private:
	// Internal implementation methods (accept transactions for batching scenarios)
	bool enter_password_impl (nano::store::transaction const &, std::string const & password);
	bool insert_watch_impl (nano::store::write_transaction const &, nano::public_key const & pub);
	nano::public_key deterministic_insert_impl (nano::store::write_transaction const &, bool generate_work = true);
	nano::public_key deterministic_insert_impl (nano::store::write_transaction const &, uint32_t index, bool generate_work = true);
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

/**
 * The wallets set is all the wallets a node controls.
 * A node may contain multiple wallets independently encrypted and operated.
 */
class wallets final
{
public:
	wallets (
	nano::node &,
	nano::wallet::wallets_backend &,
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
	std::shared_ptr<wallet> open (nano::wallet_id const &);
	std::shared_ptr<wallet> create (nano::wallet_id const &);
	std::shared_ptr<wallet> create_from_json (nano::wallet_id const &, std::string const & json);
	void destroy (nano::wallet_id const &);
	void reload ();
	void clear_send_ids ();

	// Account lookup
	std::unordered_map<nano::wallet_id, std::shared_ptr<wallet>> all_wallets ();
	bool exists (nano::account const &);
	bool exists_any (nano::account const &, nano::account const &);

	// Receivable
	bool search_receivable (nano::wallet_id const &);
	void search_receivable_all ();
	void receive_confirmed (nano::block_hash const & hash, nano::account const & destination);

	// Wallet actions queue
	void do_wallet_actions ();
	void queue_wallet_action (nano::uint128_t const & priority, std::shared_ptr<wallet> const &, std::function<void (wallet &)> action);

	// Representatives
	void foreach_representative (std::function<void (nano::public_key const &, nano::raw_key const &)> const & action);
	bool check_rep (nano::account const &);
	void refresh_reps ();
	wallet_representatives reps () const;

	/// Returns a signer that iterates over all representatives in the wallet
	using signer_t = std::function<void (std::function<void (nano::public_key const &, nano::raw_key const &)> const &)>;
	signer_t signer ();

	nano::container_info container_info () const;

private: // Transactions
	nano::store::write_transaction tx_begin_write ();
	nano::store::read_transaction tx_begin_read ();

public: // Dependencies
	nano::node & node;
	nano::wallet::wallets_backend & backend;
	nano::ledger & ledger;
	nano::node_config const & config;
	nano::network_params const & network_params;
	nano::online_reps & online_reps;
	nano::network & network;
	nano::stats & stats;
	nano::logger & logger;

public:
	std::function<void (bool)> observer;

	std::unordered_map<nano::wallet_id, std::shared_ptr<wallet>> items;
	std::multimap<nano::uint128_t, std::pair<std::shared_ptr<wallet>, std::function<void (wallet &)>>, std::greater<nano::uint128_t>> actions;
	nano::locked<std::unordered_map<nano::account, nano::root>> delayed_work;

	nano::kdf kdf;

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
	void refresh_rep_index ();
	void refresh_rep_keys_cache ();

private:
	mutable nano::locked<wallet_representatives> representatives;
	nano::locked<std::vector<std::pair<nano::public_key, std::unique_ptr<nano::fan>>>> rep_keys_cache;

	friend class wallet;
};
}
