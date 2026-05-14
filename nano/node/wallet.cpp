#include <nano/crypto_lib/random_pool.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/files.hpp>
#include <nano/lib/formatting.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/threading.hpp>
#include <nano/lib/utility.hpp>
#include <nano/lib/work_version.hpp>
#include <nano/node/cementing_set.hpp>
#include <nano/node/election.hpp>
#include <nano/node/network.hpp>
#include <nano/node/node.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/transport/traffic_type.hpp>
#include <nano/node/wallet.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/secure/ledger_set_cemented.hpp>
#include <nano/store/ledger/pending.hpp>
#include <nano/store/typed_iterator_templ.hpp>
#include <nano/wallet/wallet_value.hpp>
#include <nano/wallet/wallets_backend.hpp>

#include <boost/format.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <future>
#include <stdexcept>
#include <type_traits>

template class nano::store::typed_iterator<nano::account, nano::wallet::wallet_value>;

namespace nano::wallet
{
/*
 * wallet_cipher
 */

wallet_cipher::wallet_cipher (nano::raw_key wallet_key_a) :
	wallet_key{ wallet_key_a }
{
}

nano::raw_key wallet_cipher::encrypt (nano::raw_key const & plaintext, nano::uint128_union const & iv) const
{
	nano::raw_key result;
	result.encrypt (plaintext, wallet_key, iv);
	return result;
}

nano::raw_key wallet_cipher::decrypt (nano::uint256_union const & ciphertext, nano::uint128_union const & iv) const
{
	nano::raw_key result;
	result.decrypt (ciphertext, wallet_key, iv);
	return result;
}

nano::raw_key wallet_cipher::reseal (nano::raw_key const & new_password_key, nano::uint128_union const & iv) const
{
	nano::raw_key result;
	result.encrypt (wallet_key, new_password_key, iv);
	return result;
}

/*
 * wallet_store
 */

// Wallet version number
nano::account const wallet_store::version_special{};
// Random number used to salt private key encryption
nano::account const wallet_store::salt_special (1);
// Key used to encrypt wallet keys, encrypted itself by the user password
nano::account const wallet_store::wallet_key_special (2);
// Check value used to see if password is valid
nano::account const wallet_store::check_special (3);
// Representative account to be used if we open a new account
nano::account const wallet_store::representative_special (4);
// Wallet seed for deterministic key generation
nano::account const wallet_store::seed_special (5);
// Current key index for deterministic keys
nano::account const wallet_store::deterministic_index_special (6);
int const wallet_store::special_count (7);
std::string const wallet_store::default_password{ "" };
std::size_t const wallet_store::check_iv_index (0);
std::size_t const wallet_store::seed_iv_index (1);

wallet_store::wallet_store (nano::kdf & kdf_a, nano::store::write_transaction & transaction_a, nano::wallet::wallets_backend & backend_a, unsigned fanout_a, std::string const & wallet_a, std::string const & json_a) :
	password (0, fanout_a),
	wallet_key_mem (0, fanout_a),
	kdf (kdf_a),
	backend{ backend_a }
{
	handle = backend.wallet_open_or_create (transaction_a, wallet_a);
	try
	{
		release_assert (!backend.entry_exists (transaction_a, handle, version_special), "wallet already exists before import");
		boost::property_tree::ptree wallet_l;
		std::stringstream istream (json_a);
		try
		{
			boost::property_tree::read_json (istream, wallet_l);
		}
		catch (...)
		{
			throw std::runtime_error ("Failed to parse wallet JSON");
		}
		for (auto i (wallet_l.begin ()), n (wallet_l.end ()); i != n; ++i)
		{
			nano::account key;
			if (key.decode_hex (i->first))
			{
				throw std::runtime_error ("Failed to decode wallet key hex");
			}
			nano::raw_key value;
			if (value.decode_hex (wallet_l.get<std::string> (i->first)))
			{
				throw std::runtime_error ("Failed to decode wallet value hex");
			}
			entry_put_raw (transaction_a, key, nano::wallet::wallet_value (value, 0));
		}
		bool missing = false;
		missing |= !backend.entry_exists (transaction_a, handle, version_special);
		missing |= !backend.entry_exists (transaction_a, handle, wallet_key_special);
		missing |= !backend.entry_exists (transaction_a, handle, salt_special);
		missing |= !backend.entry_exists (transaction_a, handle, check_special);
		missing |= !backend.entry_exists (transaction_a, handle, representative_special);
		if (missing)
		{
			throw std::runtime_error ("Wallet is missing required entries");
		}
		nano::raw_key key;
		key.clear ();
		password.value_set (key);
		key = entry_get_raw (transaction_a, wallet_store::wallet_key_special).key;
		wallet_key_mem.value_set (key);
		attempt_password (transaction_a, default_password);
	}
	catch (...)
	{
		destroy (transaction_a);
		throw;
	}
}

wallet_store::wallet_store (nano::kdf & kdf_a, nano::store::write_transaction & transaction_a, nano::wallet::wallets_backend & backend_a, nano::account representative_a, unsigned fanout_a, std::string const & wallet_a) :
	password (0, fanout_a),
	wallet_key_mem (0, fanout_a),
	kdf (kdf_a),
	backend{ backend_a }
{
	handle = backend.wallet_open_or_create (transaction_a, wallet_a);
	try
	{
		if (!backend.entry_exists (transaction_a, handle, version_special))
		{
			version_put (transaction_a, version_current);
			nano::raw_key salt_l;
			random_pool::generate_block (salt_l.bytes.data (), salt_l.bytes.size ());
			entry_put_raw (transaction_a, wallet_store::salt_special, nano::wallet::wallet_value (salt_l, 0));
			// Wallet key is a fixed random key that encrypts all entries
			nano::raw_key wallet_key;
			random_pool::generate_block (wallet_key.bytes.data (), sizeof (wallet_key.bytes));
			auto password_l = derive_key (transaction_a, default_password);
			password.value_set (password_l);
			// Wallet key is encrypted by the user's password
			nano::raw_key encrypted;
			encrypted.encrypt (wallet_key, password_l, salt_l.owords[0]);
			entry_put_raw (transaction_a, wallet_store::wallet_key_special, nano::wallet::wallet_value (encrypted, 0));
			nano::raw_key wallet_key_enc;
			wallet_key_enc = encrypted;
			wallet_key_mem.value_set (wallet_key_enc);
			nano::raw_key zero;
			zero.clear ();
			nano::raw_key check;
			check.encrypt (zero, wallet_key, salt_l.owords[check_iv_index]);
			entry_put_raw (transaction_a, wallet_store::check_special, nano::wallet::wallet_value (check, 0));
			nano::raw_key rep;
			rep.bytes = representative_a.bytes;
			entry_put_raw (transaction_a, wallet_store::representative_special, nano::wallet::wallet_value (rep, 0));
			nano::raw_key seed;
			random_pool::generate_block (seed.bytes.data (), seed.bytes.size ());
			seed_set (transaction_a, seed);
			entry_put_raw (transaction_a, wallet_store::deterministic_index_special, nano::wallet::wallet_value (0, 0));
		}
		nano::raw_key key;
		key = entry_get_raw (transaction_a, wallet_store::wallet_key_special).key;
		wallet_key_mem.value_set (key);
		attempt_password (transaction_a, default_password);
	}
	catch (...)
	{
		destroy (transaction_a);
		throw;
	}
}

std::vector<nano::account> wallet_store::accounts (nano::store::transaction const & transaction_a) const
{
	std::vector<nano::account> result;
	for (auto i (begin (transaction_a)), n (end (transaction_a)); i != n; ++i)
	{
		nano::account const & account (i->first);
		result.push_back (account);
	}
	return result;
}

void wallet_store::destroy (nano::store::write_transaction const & transaction_a)
{
	backend.wallet_drop (transaction_a, handle.lock ().get ());
}

bool wallet_store::is_representative (nano::store::transaction const & transaction_a) const
{
	return exists (transaction_a, representative (transaction_a));
}

void wallet_store::representative_set (nano::store::write_transaction const & transaction_a, nano::account const & representative_a)
{
	nano::raw_key rep;
	rep.bytes = representative_a.bytes;
	entry_put_raw (transaction_a, wallet_store::representative_special, nano::wallet::wallet_value (rep, 0));
}

nano::account wallet_store::representative (nano::store::transaction const & transaction_a) const
{
	nano::wallet::wallet_value value (entry_get_raw (transaction_a, wallet_store::representative_special));
	return reinterpret_cast<nano::account const &> (value.key);
}

nano::public_key wallet_store::insert_adhoc (nano::store::write_transaction const & transaction_a, nano::raw_key const & prv)
{
	auto cipher = unlock (transaction_a);
	release_assert (cipher, "wallet is locked or password is invalid");
	nano::public_key pub (nano::pub_key (prv));
	auto ciphertext = cipher.value ().encrypt (prv, pub.owords[0]);
	entry_put_raw (transaction_a, pub, nano::wallet::wallet_value (ciphertext, 0));
	return pub;
}

bool wallet_store::insert_watch (nano::store::write_transaction const & transaction_a, nano::account const & pub_a)
{
	bool error (!valid_public_key (pub_a));
	if (!error)
	{
		entry_put_raw (transaction_a, pub_a, nano::wallet::wallet_value (nano::raw_key (0), 0));
	}
	return error;
}

void wallet_store::erase (nano::store::write_transaction const & transaction_a, nano::account const & pub)
{
	backend.entry_del (transaction_a, handle, pub);
}

nano::wallet::wallet_value wallet_store::entry_get_raw (nano::store::transaction const & transaction_a, nano::account const & pub_a) const
{
	auto value = backend.entry_get (transaction_a, handle, pub_a);
	if (value)
	{
		return nano::wallet::wallet_value{ *value };
	}
	nano::wallet::wallet_value result;
	result.key.clear ();
	result.work = 0;
	return result;
}

void wallet_store::entry_put_raw (nano::store::write_transaction const & transaction_a, nano::account const & pub_a, nano::wallet::wallet_value const & entry_a)
{
	backend.entry_put (transaction_a, handle, pub_a, entry_a);
}

nano::wallet::key_type wallet_store::key_type (nano::wallet::wallet_value const & value_a) const
{
	auto number (value_a.key.number ());
	nano::wallet::key_type result;
	auto text (number.convert_to<std::string> ());
	if (number > std::numeric_limits<uint64_t>::max ())
	{
		result = key_type::adhoc;
	}
	else
	{
		if ((number >> 32).convert_to<uint32_t> () == 1)
		{
			result = key_type::deterministic;
		}
		else
		{
			result = key_type::unknown;
		}
	}
	return result;
}

nano::result<nano::raw_key> wallet_store::fetch (nano::store::transaction const & transaction, nano::account const & pub) const
{
	auto cipher = unlock (transaction);
	if (!cipher)
	{
		return nano::error (nano::error_common::wallet_locked);
	}

	auto value = entry_get_raw (transaction, pub);
	if (value.key.is_zero ())
	{
		return nano::error (nano::error_common::account_not_found_wallet);
	}

	nano::raw_key prv;
	switch (key_type (value))
	{
		case key_type::deterministic:
		{
			auto seed_l = seed_decrypt (transaction, cipher.value ());
			auto index = static_cast<uint32_t> (value.key.number () & static_cast<uint32_t> (-1));
			prv = nano::deterministic_key (seed_l, index);
			break;
		}
		case key_type::adhoc:
		{
			prv = cipher.value ().decrypt (value.key, pub.owords[0]);
			break;
		}
		default:
		{
			return nano::error (nano::error_common::bad_private_key);
		}
	}

	// Verify the key
	nano::public_key compare = nano::pub_key (prv);
	if (pub != compare)
	{
		return nano::error (nano::error_common::bad_private_key);
	}

	return prv;
}

bool wallet_store::valid_public_key (nano::public_key const & pub) const
{
	return pub.number () >= special_count;
}

bool wallet_store::exists (nano::store::transaction const & transaction_a, nano::account const & pub) const
{
	return valid_public_key (pub) && find (transaction_a, pub) != end (transaction_a);
}

void wallet_store::serialize_json (nano::store::transaction const & transaction_a, std::string & string_a) const
{
	boost::property_tree::ptree tree;
	// Iterate from account 0 to include the specials slots in the serialized output.
	for (iterator i{ backend.entries_begin (transaction_a, handle) }, n{ backend.entries_end (transaction_a, handle) }; i != n; ++i)
	{
		tree.put (i->first.to_string (), i->second.key.to_string ());
	}
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree);
	string_a = ostream.str ();
}

void wallet_store::write_backup (nano::store::transaction const & transaction_a, std::filesystem::path const & path_a) const
{
	std::ofstream backup_file;
	backup_file.open (path_a.string ());
	if (!backup_file.fail ())
	{
		// Set permissions to 600
		boost::system::error_code ec;
		nano::set_secure_perm_file (path_a, ec);

		std::string json;
		serialize_json (transaction_a, json);
		backup_file << json;
	}
}

nano::result<bool> wallet_store::move (nano::store::write_transaction const & transaction_a, wallet_store & other_a, std::vector<nano::public_key> const & keys)
{
	if (!valid_password (transaction_a) || !other_a.valid_password (transaction_a))
	{
		return nano::error (nano::error_common::wallet_locked);
	}

	bool error = false;
	for (auto i (keys.begin ()), n (keys.end ()); i != n; ++i)
	{
		auto prv_result = other_a.fetch (transaction_a, *i);
		if (prv_result)
		{
			insert_adhoc (transaction_a, prv_result.value ());
			other_a.erase (transaction_a, *i);
		}
		else
		{
			error = true;
		}
	}
	return error;
}

nano::result<bool> wallet_store::import (nano::store::write_transaction const & transaction_a, wallet_store & other_a)
{
	if (!valid_password (transaction_a) || !other_a.valid_password (transaction_a))
	{
		return nano::error (nano::error_common::wallet_locked);
	}

	bool error = false;
	for (auto i (other_a.begin (transaction_a)), n (other_a.end (transaction_a)); i != n; ++i)
	{
		auto prv_result = other_a.fetch (transaction_a, i->first);
		if (prv_result)
		{
			if (!prv_result.value ().is_zero ())
			{
				insert_adhoc (transaction_a, prv_result.value ());
			}
			else
			{
				insert_watch (transaction_a, i->first);
			}
			other_a.erase (transaction_a, i->first);
		}
		else
		{
			error = true;
		}
	}
	return error;
}

std::optional<uint64_t> wallet_store::work_get (nano::store::transaction const & transaction, nano::public_key const & pub) const
{
	auto entry = entry_get_raw (transaction, pub);
	if (!entry.key.is_zero ())
	{
		return entry.work;
	}
	return std::nullopt;
}

void wallet_store::work_put (nano::store::write_transaction const & transaction_a, nano::public_key const & pub_a, uint64_t work_a)
{
	auto entry (entry_get_raw (transaction_a, pub_a));
	debug_assert (!entry.key.is_zero ());
	entry.work = work_a;
	entry_put_raw (transaction_a, pub_a, entry);
}

unsigned wallet_store::version (nano::store::transaction const & transaction_a) const
{
	nano::wallet::wallet_value value (entry_get_raw (transaction_a, wallet_store::version_special));
	auto entry (value.key);
	auto result (static_cast<unsigned> (entry.bytes[31]));
	return result;
}

void wallet_store::version_put (nano::store::write_transaction const & transaction_a, unsigned version_a)
{
	nano::raw_key entry (version_a);
	entry_put_raw (transaction_a, wallet_store::version_special, nano::wallet::wallet_value (entry, 0));
}

nano::uint256_union wallet_store::check_value_get (nano::store::transaction const & transaction_a) const
{
	auto value = entry_get_raw (transaction_a, wallet_store::check_special);
	return value.key;
}

nano::uint256_union wallet_store::salt_get (nano::store::transaction const & transaction_a) const
{
	auto value = entry_get_raw (transaction_a, wallet_store::salt_special);
	return value.key;
}

std::optional<wallet_cipher> wallet_store::unlock (nano::store::transaction const & transaction_a) const
{
	auto const wallet_key_l = wallet_key_decrypt (transaction_a);
	nano::raw_key zero{};
	zero.clear ();
	nano::uint256_union check_l{};
	check_l.encrypt (zero, wallet_key_l, salt_get (transaction_a).owords[check_iv_index]);
	if (check_value_get (transaction_a) != check_l)
	{
		return std::nullopt;
	}
	return wallet_cipher{ wallet_key_l };
}

nano::raw_key wallet_store::wallet_key_decrypt (nano::store::transaction const & transaction_a) const
{
	nano::lock_guard<std::recursive_mutex> lock{ mutex };
	nano::raw_key wallet_l;
	wallet_key_mem.value (wallet_l);
	nano::raw_key password_l;
	password.value (password_l);
	nano::raw_key result;
	result.decrypt (wallet_l, password_l, salt_get (transaction_a).owords[0]);
	return result;
}

nano::raw_key wallet_store::seed (nano::store::transaction const & transaction) const
{
	auto cipher = unlock (transaction);
	release_assert (cipher, "wallet is locked or password is invalid");
	return seed_decrypt (transaction, cipher.value ());
}

nano::raw_key wallet_store::seed_decrypt (nano::store::transaction const & transaction, nano::wallet::wallet_cipher const & cipher) const
{
	auto encrypted_seed = entry_get_raw (transaction, wallet_store::seed_special).key;
	return cipher.decrypt (encrypted_seed, salt_get (transaction).owords[seed_iv_index]);
}

void wallet_store::seed_set (nano::store::write_transaction const & transaction_a, nano::raw_key const & prv_a)
{
	auto cipher = unlock (transaction_a);
	release_assert (cipher, "wallet is locked or password is invalid");
	auto ciphertext = cipher.value ().encrypt (prv_a, salt_get (transaction_a).owords[seed_iv_index]);
	entry_put_raw (transaction_a, wallet_store::seed_special, nano::wallet::wallet_value (ciphertext, 0));
	deterministic_clear (transaction_a);
}

nano::public_key wallet_store::deterministic_insert (nano::store::write_transaction const & transaction_a)
{
	auto index (deterministic_index_get (transaction_a));
	auto prv = deterministic_key (transaction_a, index);
	nano::public_key result (nano::pub_key (prv));
	while (exists (transaction_a, result))
	{
		++index;
		prv = deterministic_key (transaction_a, index);
		result = nano::pub_key (prv);
	}
	uint64_t marker (1);
	marker <<= 32;
	marker |= index;
	entry_put_raw (transaction_a, result, nano::wallet::wallet_value (marker, 0));
	++index;
	deterministic_index_set (transaction_a, index);
	return result;
}

nano::public_key wallet_store::deterministic_insert (nano::store::write_transaction const & transaction_a, uint32_t const index)
{
	auto prv = deterministic_key (transaction_a, index);
	nano::public_key result (nano::pub_key (prv));
	uint64_t marker (1);
	marker <<= 32;
	marker |= index;
	entry_put_raw (transaction_a, result, nano::wallet::wallet_value (marker, 0));
	return result;
}

nano::raw_key wallet_store::deterministic_key (nano::store::transaction const & transaction, uint32_t index) const
{
	auto cipher = unlock (transaction);
	release_assert (cipher, "wallet is locked or password is invalid");
	auto wallet_seed = seed_decrypt (transaction, cipher.value ());
	return nano::deterministic_key (wallet_seed, index);
}

uint32_t wallet_store::deterministic_index_get (nano::store::transaction const & transaction_a) const
{
	nano::wallet::wallet_value value (entry_get_raw (transaction_a, wallet_store::deterministic_index_special));
	return static_cast<uint32_t> (value.key.number () & static_cast<uint32_t> (-1));
}

void wallet_store::deterministic_index_set (nano::store::write_transaction const & transaction_a, uint32_t index_a)
{
	nano::raw_key index_l (index_a);
	nano::wallet::wallet_value value (index_l, 0);
	entry_put_raw (transaction_a, wallet_store::deterministic_index_special, value);
}

void wallet_store::deterministic_clear (nano::store::write_transaction const & transaction_a)
{
	nano::uint256_union key (0);
	for (auto i (begin (transaction_a)), n (end (transaction_a)); i != n;)
	{
		switch (key_type (nano::wallet::wallet_value (i->second)))
		{
			case key_type::deterministic:
			{
				auto const & key (i->first);
				erase (transaction_a, key);
				i = begin (transaction_a, key);
				break;
			}
			default:
			{
				++i;
				break;
			}
		}
	}
	deterministic_index_set (transaction_a, 0);
}

bool wallet_store::valid_password (nano::store::transaction const & transaction_a) const
{
	return unlock (transaction_a).has_value ();
}

bool wallet_store::attempt_password (nano::store::transaction const & transaction_a, std::string const & password_a)
{
	bool result = false;
	{
		nano::lock_guard<std::recursive_mutex> lock{ mutex };
		auto password_l = derive_key (transaction_a, password_a);
		password.value_set (password_l);
		result = !valid_password (transaction_a);
	}
	if (!result)
	{
		switch (version (transaction_a))
		{
			case version_4:
				break;
			default:
				debug_assert (false);
		}
	}
	return result;
}

bool wallet_store::rekey (nano::store::write_transaction const & transaction_a, std::string const & password_a)
{
	nano::lock_guard<std::recursive_mutex> lock{ mutex };
	auto cipher = unlock (transaction_a);
	if (!cipher)
	{
		return true;
	}
	auto password_new = derive_key (transaction_a, password_a);
	auto encrypted = cipher.value ().reseal (password_new, salt_get (transaction_a).owords[0]);
	password.value_set (password_new);
	wallet_key_mem.value_set (encrypted);
	entry_put_raw (transaction_a, wallet_store::wallet_key_special, nano::wallet::wallet_value (encrypted, 0));
	return false;
}

nano::raw_key wallet_store::derive_key (nano::store::transaction const & transaction_a, std::string const & password_a) const
{
	auto const salt_l = salt_get (transaction_a);
	nano::raw_key result;
	kdf.phs (result, password_a, salt_l);
	return result;
}

auto wallet_store::begin (nano::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.entries_begin (txn, handle, nano::account{ special_count }) };
}

auto wallet_store::begin (nano::store::transaction const & txn, nano::account const & key) const -> iterator
{
	return iterator{ backend.entries_begin (txn, handle, key) };
}

auto wallet_store::end (nano::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.entries_end (txn, handle) };
}

auto wallet_store::find (nano::store::transaction const & txn, nano::account const & key) const -> iterator
{
	auto it = begin (txn, key);
	auto end_it = end (txn);
	if (it == end_it)
	{
		return end_it;
	}
	if (it->first == key)
	{
		return it;
	}
	return end_it;
}

/*
 * wallet
 */

wallet::wallet (nano::store::write_transaction & transaction_a, nano::wallet::wallets & wallets_a, std::string const & wallet_a) :
	lock_observer ([] (bool, bool) {}),
	store (wallets_a.kdf, transaction_a, wallets_a.backend, wallets_a.config.random_representative (), wallets_a.config.password_fanout, wallet_a),
	wallets (wallets_a),
	logger (wallets_a.logger)
{
}

wallet::wallet (nano::store::write_transaction & transaction_a, nano::wallet::wallets & wallets_a, std::string const & wallet_a, std::string const & json) :
	lock_observer ([] (bool, bool) {}),
	store (wallets_a.kdf, transaction_a, wallets_a.backend, wallets_a.config.password_fanout, wallet_a, json),
	wallets (wallets_a),
	logger (wallets_a.logger)
{
}

void wallet::enter_initial_password ()
{
	nano::raw_key password_l;
	{
		nano::lock_guard<std::recursive_mutex> lock{ store.mutex };
		store.password.value (password_l);
	}
	if (password_l.is_zero ())
	{
		auto transaction (wallets.tx_begin_read ());
		enter_password_impl (transaction, wallet_store::default_password);
	}
}

bool wallet::enter_password (std::string const & password_a)
{
	bool result;
	{
		auto transaction = wallets.tx_begin_write ();
		result = enter_password_impl (transaction, password_a);
	}
	if (!result)
	{
		wallets.refresh_rep_keys_cache ();
	}
	return result;
}

bool wallet::enter_password_impl (nano::store::transaction const & transaction_a, std::string const & password_a)
{
	auto result (store.attempt_password (transaction_a, password_a));
	if (!result)
	{
		logger.info (nano::log::type::wallet, "Wallet unlocked");

		auto this_l = shared_from_this ();
		wallets.queue_wallet_action (wallets::high_priority, this_l, [this_l] (wallet & wallet) {
			// Wallets must survive node lifetime
			this_l->search_receivable ();
		});
	}
	else
	{
		logger.warn (nano::log::type::wallet, "Invalid password, wallet locked");
	}
	lock_observer (result, password_a.empty ());
	return result;
}

nano::public_key wallet::deterministic_insert_impl (nano::store::write_transaction const & transaction, bool generate_work)
{
	auto key = store.deterministic_insert (transaction);

	logger.info (nano::log::type::wallet, "Deterministically inserted new account: {}", key.to_account ());

	if (generate_work)
	{
		work_ensure (key, key);
	}

	if (wallets.check_rep (key))
	{
		logger.info (nano::log::type::wallet, "New account qualified as a representative: {}", key.to_account ());
		representatives.lock ()->insert (key);
	}

	return key;
}

nano::public_key wallet::deterministic_insert_impl (nano::store::write_transaction const & transaction, uint32_t index, bool generate_work)
{
	auto key = store.deterministic_insert (transaction, index);

	logger.info (nano::log::type::wallet, "Deterministically inserted new account: {} with index: {}", key.to_account (), index);

	if (generate_work)
	{
		work_ensure (key, key);
	}

	if (wallets.check_rep (key))
	{
		logger.info (nano::log::type::wallet, "New account qualified as a representative: {}", key.to_account ());
		representatives.lock ()->insert (key);
	}

	return key;
}

nano::result<nano::public_key> wallet::deterministic_insert (uint32_t index, bool generate_work)
{
	auto transaction = wallets.tx_begin_write ();

	if (!store.valid_password (transaction))
	{
		return nano::error (nano::error_common::wallet_locked);
	}

	auto result = deterministic_insert_impl (transaction, index, generate_work);
	transaction.commit ();
	wallets.refresh_rep_keys_cache ();
	return result;
}

nano::result<nano::public_key> wallet::deterministic_insert (bool generate_work)
{
	auto transaction = wallets.tx_begin_write ();

	if (!store.valid_password (transaction))
	{
		return nano::error (nano::error_common::wallet_locked);
	}

	auto result = deterministic_insert_impl (transaction, generate_work);
	transaction.commit ();
	wallets.refresh_rep_keys_cache ();
	return result;
}

nano::result<nano::public_key> wallet::insert_adhoc (nano::raw_key const & prv, bool generate_work)
{
	auto transaction = wallets.tx_begin_write ();

	if (!store.valid_password (transaction))
	{
		return nano::error (nano::error_common::wallet_locked);
	}

	auto key = store.insert_adhoc (transaction, prv);

	logger.info (nano::log::type::wallet, "Ad-hoc inserted new account: {}", key.to_account ());

	if (generate_work)
	{
		auto ledger_txn = wallets.ledger.tx_begin_read ();
		work_ensure (key, wallets.ledger.latest_root (ledger_txn, key));
	}

	// Makes sure that the representatives container will be in sync with any added keys
	transaction.commit ();

	if (wallets.check_rep (key))
	{
		logger.info (nano::log::type::wallet, "New account qualified as a representative: {}", key.to_account ());
		representatives.lock ()->insert (key);
		wallets.refresh_rep_keys_cache ();
	}

	return key;
}

bool wallet::insert_watch (nano::public_key const & pub_a)
{
	auto transaction = wallets.tx_begin_write ();
	return insert_watch_impl (transaction, pub_a);
}

bool wallet::insert_watch_impl (nano::store::write_transaction const & transaction_a, nano::public_key const & pub_a)
{
	return store.insert_watch (transaction_a, pub_a);
}

bool wallet::exists (nano::public_key const & account_a)
{
	auto transaction (wallets.tx_begin_read ());
	return store.exists (transaction, account_a);
}

bool wallet::import (std::string const & json_a, std::string const & password_a)
{
	bool error (true);
	auto transaction (wallets.tx_begin_write ());
	nano::uint256_union id;
	random_pool::generate_block (id.bytes.data (), id.bytes.size ());
	try
	{
		auto temp = std::make_unique<wallet_store> (wallets.kdf, transaction, wallets.backend, 1, id.to_string (), json_a);
		if (!temp->attempt_password (transaction, password_a))
		{
			auto result = store.import (transaction, *temp);
			error = !result || result.value ();
		}
		temp->destroy (transaction);
	}
	catch (std::exception const & ex)
	{
		logger.error (nano::log::type::wallet, "Failed to import wallet: {}", ex.what ());
	}
	return error;
}

void wallet::serialize_json (std::string & json_a)
{
	auto transaction (wallets.tx_begin_read ());
	store.serialize_json (transaction, json_a);
}

void wallet::write_backup (std::filesystem::path const & path_a)
{
	auto transaction (wallets.tx_begin_read ());
	store.write_backup (transaction, path_a);
}

std::shared_ptr<nano::block> wallet::receive_action (nano::block_hash const & send_hash_a, nano::account const & representative_a, nano::uint128_union const & amount_a, nano::account const & account_a, uint64_t work_a, bool generate_work_a)
{
	std::shared_ptr<nano::block> block;
	nano::block_details details;
	details.is_receive = true;
	if (wallets.config.receive_minimum.number () <= amount_a.number ())
	{
		auto ledger_txn = wallets.ledger.tx_begin_read ();
		auto transaction (wallets.tx_begin_read ());
		if (wallets.ledger.any.block_exists_or_pruned (ledger_txn, send_hash_a))
		{
			auto pending_info = wallets.ledger.any.pending_get (ledger_txn, nano::pending_key (account_a, send_hash_a));
			if (pending_info)
			{
				auto prv_result = store.fetch (transaction, account_a);
				if (prv_result)
				{
					logger.info (nano::log::type::wallet, "Receiving block: {} from account: {}, amount: {} raw",
					send_hash_a,
					account_a,
					nano::log::as_raw_nano (pending_info->amount));

					if (work_a == 0)
					{
						work_a = store.work_get (transaction, account_a).value_or (0);
					}
					auto info = wallets.ledger.any.account_get (ledger_txn, account_a);
					if (info)
					{
						block = std::make_shared<nano::state_block> (account_a, info->head, info->representative, info->balance.number () + pending_info->amount.number (), send_hash_a, prv_result.value (), account_a, work_a);
						details.epoch = std::max (info->epoch (), pending_info->epoch);
					}
					else
					{
						block = std::make_shared<nano::state_block> (account_a, 0, representative_a, pending_info->amount, reinterpret_cast<nano::link const &> (send_hash_a), prv_result.value (), account_a, work_a);
						details.epoch = pending_info->epoch;
					}
				}
				else
				{
					logger.warn (nano::log::type::wallet, "Unable to receive, wallet locked, block: {} to account: {}",
					send_hash_a,
					account_a);
				}
			}
			else
			{
				// Ledger doesn't have this marked as available to receive anymore
				logger.warn (nano::log::type::wallet, "Not receiving block: {}, block already received", send_hash_a);
			}
		}
		else
		{
			// Ledger doesn't have this block anymore.
			logger.warn (nano::log::type::wallet, "Not receiving block: {}, block no longer exists or pruned", send_hash_a);
		}
	}
	else
	{
		// Someone sent us something below the threshold of receiving
		logger.warn (nano::log::type::wallet, "Not receiving block: {} due to minimum receive threshold", send_hash_a);
	}
	if (block != nullptr)
	{
		if (action_complete (block, account_a, generate_work_a, details))
		{
			// Return null block after work generation or ledger process error
			block = nullptr;
		}
	}
	return block;
}

std::shared_ptr<nano::block> wallet::change_action (nano::account const & source_a, nano::account const & representative_a, uint64_t work_a, bool generate_work_a)
{
	std::shared_ptr<nano::block> block;
	nano::block_details details;
	{
		auto transaction (wallets.tx_begin_read ());
		auto ledger_txn = wallets.ledger.tx_begin_read ();
		if (store.valid_password (transaction))
		{
			auto existing (store.find (transaction, source_a));
			if (existing != store.end (transaction) && !wallets.ledger.any.account_head (ledger_txn, source_a).is_zero ())
			{
				logger.info (nano::log::type::wallet, "Changing representative for account: {} to: {}",
				source_a,
				representative_a);

				auto info = wallets.ledger.any.account_get (ledger_txn, source_a);
				release_assert (info, "could not find account info for account in wallet change_action", source_a.to_account ());
				auto prv_result = store.fetch (transaction, source_a);
				release_assert (prv_result, "failed to fetch private key for account in wallet change_action", source_a.to_account ());
				if (work_a == 0)
				{
					work_a = store.work_get (transaction, source_a).value_or (0);
				}
				block = std::make_shared<nano::state_block> (source_a, info->head, representative_a, info->balance, 0, prv_result.value (), source_a, work_a);
				details.epoch = info->epoch ();
			}
			else
			{
				logger.warn (nano::log::type::wallet, "Changing representative for account: {} failed, wallet locked or account not found", source_a);
			}
		}
		else
		{
			logger.warn (nano::log::type::wallet, "Changing representative for account: {} failed, wallet locked", source_a);
		}
	}
	if (block != nullptr)
	{
		if (action_complete (block, source_a, generate_work_a, details))
		{
			// Return null block after work generation or ledger process error
			block = nullptr;
		}
	}
	return block;
}

std::shared_ptr<nano::block> wallet::send_action (nano::account const & source_a, nano::account const & account_a, nano::uint128_t const & amount_a, uint64_t work_a, bool generate_work_a, std::optional<std::string> id_a)
{
	auto prepare_send = [this, &wallets = this->wallets, &store = this->store, &source_a, &amount_a, &work_a, &account_a, &id_a] (auto const & transaction) {
		auto ledger_txn = wallets.ledger.tx_begin_read ();
		auto error (false);
		auto cached_block (false);
		std::shared_ptr<nano::block> block;
		nano::block_details details;
		details.is_send = true;
		if (id_a)
		{
			auto existing_value = wallets.backend.send_action_id_get (transaction, *id_a);
			if (existing_value)
			{
				auto existing_hash = static_cast<nano::block_hash> (*existing_value);
				block = wallets.ledger.any.block_get (ledger_txn, existing_hash);
				if (block != nullptr)
				{
					logger.warn (nano::log::type::wallet, "Block already exists for send action with id: {}, existing hash: {}",
					id_a.value (),
					existing_hash);

					cached_block = true;
					wallets.network.flood_block (block, nano::transport::traffic_type::block_broadcast_initial);
				}
				else
				{
					logger.warn (nano::log::type::wallet, "Block was not found in ledger for send action with id: {}, hash: {}",
					id_a.value (),
					existing_hash);
				}
			}
		}
		if (!error && block == nullptr)
		{
			if (store.valid_password (transaction))
			{
				auto existing (store.find (transaction, source_a));
				if (existing != store.end (transaction))
				{
					auto balance (wallets.ledger.any.account_balance (ledger_txn, source_a));
					if (balance && balance.value ().number () >= amount_a)
					{
						logger.info (nano::log::type::wallet, "Sending from account: {} to: {}, amount: {} raw",
						source_a,
						account_a,
						nano::log::as_raw_nano (amount_a));

						auto info = wallets.ledger.any.account_get (ledger_txn, source_a);
						release_assert (info, "could not find account info for account in wallet send_action", source_a.to_account ());
						auto prv_result = store.fetch (transaction, source_a);
						release_assert (prv_result, "failed to fetch private key for account in wallet send_action", source_a.to_account ());
						if (work_a == 0)
						{
							work_a = store.work_get (transaction, source_a).value_or (0);
						}
						block = std::make_shared<nano::state_block> (source_a, info->head, info->representative, balance.value ().number () - amount_a, account_a, prv_result.value (), source_a, work_a);
						details.epoch = info->epoch ();
						if (id_a && block != nullptr)
						{
							// `id_a` being set implies the caller passed a write transaction (see below).
							// `if constexpr` keeps the put out of the read-txn instantiation of this lambda.
							if constexpr (std::is_same_v<std::decay_t<decltype (transaction)>, nano::store::write_transaction>)
							{
								if (!wallets.backend.send_action_id_put (transaction, *id_a, block->hash ()))
								{
									block = nullptr;
									error = true;
								}
							}
							else
							{
								release_assert (false, "send_action with id requires a write transaction");
							}
						}
					}
					else
					{
						if (balance)
						{
							logger.warn (nano::log::type::wallet, "Insufficient balance for send from: {}, required: {} raw, available: {} raw",
							source_a,
							nano::log::as_raw_nano (amount_a),
							nano::log::as_raw_nano (balance.value ()));
						}
						else
						{
							logger.warn (nano::log::type::wallet, "Insufficient balance for send from: {}, required: {} raw, available: unknown",
							source_a,
							nano::log::as_raw_nano (amount_a));
						}
					}
				}
			}
		}
		return std::make_tuple (block, error, cached_block, details);
	};

	std::tuple<std::shared_ptr<nano::block>, bool, bool, nano::block_details> result;
	{
		if (id_a)
		{
			result = prepare_send (wallets.tx_begin_write ());
		}
		else
		{
			result = prepare_send (wallets.tx_begin_read ());
		}
	}

	std::shared_ptr<nano::block> block;
	bool error;
	bool cached_block;
	nano::block_details details;
	std::tie (block, error, cached_block, details) = result;

	if (!error && block != nullptr && !cached_block)
	{
		if (action_complete (block, source_a, generate_work_a, details))
		{
			// Return null block after work generation or ledger process error
			block = nullptr;
		}
	}
	return block;
}

bool wallet::action_complete (std::shared_ptr<nano::block> const & block_a, nano::account const & account_a, bool const generate_work_a, nano::block_details const & details_a)
{
	bool error{ false };
	// Unschedule any work caching for this account
	wallets.delayed_work->erase (account_a);
	if (block_a != nullptr)
	{
		auto required_difficulty{ wallets.network_params.work.threshold (block_a->work_version (), details_a) };
		if (wallets.network_params.work.difficulty (*block_a) < required_difficulty)
		{
			logger.info (nano::log::type::wallet, "Cached or provided work for block: {}, account {}: is invalid, regenerating...",
			block_a->hash (),
			account_a);

			debug_assert (required_difficulty <= wallets.node.max_work_generate_difficulty (block_a->work_version ()));
			error = !wallets.node.work_generate_blocking (*block_a, required_difficulty).has_value ();
		}
		if (!error)
		{
			auto result = wallets.node.process_local (block_a);
			error = !result || result.value () != nano::block_status::progress;
			debug_assert (error || block_a->sideband ().details == details_a);
		}
		if (!error && generate_work_a)
		{
			// Pregenerate work for next block based on the block just created
			work_ensure (account_a, block_a->hash ());
		}
	}
	return error;
}

bool wallet::change_sync (nano::account const & source_a, nano::account const & representative_a)
{
	std::promise<bool> result;
	std::future<bool> future = result.get_future ();
	change_async (
	source_a, representative_a, [&result] (std::shared_ptr<nano::block> const & block_a) {
		result.set_value (block_a == nullptr);
	},
	true);
	return future.get ();
}

void wallet::change_async (nano::account const & source_a, nano::account const & representative_a, std::function<void (std::shared_ptr<nano::block> const &)> const & action_a, uint64_t work_a, bool generate_work_a)
{
	auto this_l (shared_from_this ());
	wallets.queue_wallet_action (wallets::high_priority, this_l, [this_l, source_a, representative_a, action_a, work_a, generate_work_a] (wallet & wallet_a) {
		auto block (wallet_a.change_action (source_a, representative_a, work_a, generate_work_a));
		action_a (block);
	});
}

bool wallet::receive_sync (std::shared_ptr<nano::block> const & block_a, nano::account const & representative_a, nano::uint128_t const & amount_a)
{
	std::promise<bool> result;
	std::future<bool> future = result.get_future ();
	receive_async (
	block_a->hash (), representative_a, amount_a, block_a->destination (), [&result] (std::shared_ptr<nano::block> const & block_a) {
		result.set_value (block_a == nullptr);
	},
	true);
	return future.get ();
}

void wallet::receive_async (nano::block_hash const & hash_a, nano::account const & representative_a, nano::uint128_t const & amount_a, nano::account const & account_a, std::function<void (std::shared_ptr<nano::block> const &)> const & action_a, uint64_t work_a, bool generate_work_a)
{
	auto this_l (shared_from_this ());
	wallets.queue_wallet_action (amount_a, this_l, [this_l, hash_a, representative_a, amount_a, account_a, action_a, work_a, generate_work_a] (wallet & wallet_a) {
		auto block (wallet_a.receive_action (hash_a, representative_a, amount_a, account_a, work_a, generate_work_a));
		action_a (block);
	});
}

nano::block_hash wallet::send_sync (nano::account const & source_a, nano::account const & account_a, nano::uint128_t const & amount_a)
{
	std::promise<nano::block_hash> result;
	std::future<nano::block_hash> future = result.get_future ();
	send_async (
	source_a, account_a, amount_a, [&result] (std::shared_ptr<nano::block> const & block_a) {
		result.set_value (block_a->hash ());
	},
	true);
	return future.get ();
}

void wallet::send_async (nano::account const & source_a, nano::account const & account_a, nano::uint128_t const & amount_a, std::function<void (std::shared_ptr<nano::block> const &)> const & action_a, uint64_t work_a, bool generate_work_a, std::optional<std::string> id_a)
{
	auto this_l (shared_from_this ());
	wallets.queue_wallet_action (wallets::high_priority, this_l, [this_l, source_a, account_a, amount_a, action_a, work_a, generate_work_a, id_a] (wallet & wallet_a) {
		auto block (wallet_a.send_action (source_a, account_a, amount_a, work_a, generate_work_a, id_a));
		action_a (block);
	});
}

// Update work for account if latest root is root_a
void wallet::work_update_impl (nano::store::write_transaction const & transaction_a, nano::account const & account_a, nano::root const & root_a, uint64_t work_a)
{
	debug_assert (!wallets.network_params.work.validate_entry (nano::work_version::work_1, root_a, work_a));
	debug_assert (store.exists (transaction_a, account_a));
	auto ledger_txn = wallets.ledger.tx_begin_read ();
	auto latest (wallets.ledger.latest_root (ledger_txn, account_a));
	if (latest == root_a)
	{
		store.work_put (transaction_a, account_a, work_a);
	}
	else
	{
		logger.warn (nano::log::type::wallet, "Cached work no longer valid, discarding");
	}
}

void wallet::work_ensure (nano::account const & account_a, nano::root const & root_a)
{
	using namespace std::chrono_literals;
	std::chrono::seconds const precache_delay = wallets.network_params.network.is_dev_network () ? 1s : 10s;

	wallets.delayed_work->operator[] (account_a) = root_a;

	wallets.workers.post_delayed (precache_delay, [this_l = shared_from_this (), account_a, root_a] {
		if (this_l->wallets.stopped)
		{
			return;
		}
		auto delayed_work = this_l->wallets.delayed_work.lock ();
		auto existing (delayed_work->find (account_a));
		if (existing != delayed_work->end () && existing->second == root_a)
		{
			delayed_work->erase (existing);
			this_l->wallets.queue_wallet_action (wallets::generate_priority, this_l, [account_a, root_a] (wallet & wallet_a) {
				wallet_a.work_cache_blocking (account_a, root_a);
			});
		}
	});
}

bool wallet::search_receivable ()
{
	auto transaction = wallets.tx_begin_read ();
	return search_receivable_impl (transaction);
}

bool wallet::search_receivable_impl (nano::store::transaction const & wallet_transaction_a)
{
	auto result (!store.valid_password (wallet_transaction_a));
	if (!result)
	{
		logger.debug (nano::log::type::wallet, "Beginning receivable block search");

		for (auto i (store.begin (wallet_transaction_a)), n (store.end (wallet_transaction_a)); i != n; ++i)
		{
			auto ledger_txn = wallets.ledger.tx_begin_read ();
			nano::account const & account (i->first);
			// Don't search pending for watch-only accounts
			if (!nano::wallet::wallet_value (i->second).key.is_zero ())
			{
				for (auto j (wallets.ledger.store.pending.begin (ledger_txn, nano::pending_key (account, 0))), k (wallets.ledger.store.pending.end (ledger_txn)); j != k && nano::pending_key (j->first).account == account; ++j)
				{
					nano::pending_key key (j->first);
					auto hash (key.hash);
					nano::pending_info pending (j->second);
					auto amount (pending.amount.number ());
					if (wallets.config.receive_minimum.number () <= amount)
					{
						bool const confirmed = wallets.ledger.cemented.block_exists_or_pruned (ledger_txn, hash);

						logger.info (nano::log::type::wallet, "Found a receivable block: {} ({}) for account: {} from: {}",
						hash,
						confirmed ? "confirmed" : "unconfirmed",
						key.account,
						pending.source);

						if (confirmed)
						{
							auto representative = store.representative (wallet_transaction_a);
							// Receive confirmed block
							receive_async (hash, representative, amount, account, [] (std::shared_ptr<nano::block> const &) {});
						}
						else if (!wallets.node.cementing_set.contains (hash))
						{
							auto block = wallets.ledger.any.block_get (ledger_txn, hash);
							if (block)
							{
								// Request confirmation for block which is not being processed yet
								wallets.node.start_election (block);
							}
						}
					}
				}
			}
		}

		logger.debug (nano::log::type::wallet, "Receivable block search phase complete");
	}
	else
	{
		logger.warn (nano::log::type::wallet, "Unable to search receivable blocks, wallet is locked. Blocks won't be auto-received until the wallet is unlocked");
	}
	return result;
}

void wallet::init_free_accounts_impl (nano::store::transaction const & transaction_a)
{
	free_accounts.clear ();
	for (auto i (store.begin (transaction_a)), n (store.end (transaction_a)); i != n; ++i)
	{
		free_accounts.insert (i->first);
	}
}

uint32_t wallet::deterministic_check (uint32_t index)
{
	auto transaction = wallets.tx_begin_read ();
	return deterministic_check_impl (transaction, index);
}

uint32_t wallet::deterministic_check_impl (nano::store::transaction const & transaction_a, uint32_t index)
{
	auto ledger_txn = wallets.ledger.tx_begin_read ();
	for (uint32_t i (index + 1), n (index + 64); i < n; ++i)
	{
		auto prv = store.deterministic_key (transaction_a, i);
		nano::keypair pair (prv.to_string ());
		// Check if account received at least 1 block
		auto latest (wallets.ledger.any.account_head (ledger_txn, pair.pub));
		if (!latest.is_zero ())
		{
			index = i;
			// i + 64 - Check additional 64 accounts
			// i/64 - Check additional accounts for large wallets. I.e. 64000/64 = 1000 accounts to check
			n = i + 64 + (i / 64);
		}
		else
		{
			// Check if there are pending blocks for account
			auto current = wallets.ledger.any.receivable_upper_bound (ledger_txn, pair.pub, 0);
			if (current != wallets.ledger.any.receivable_end ())
			{
				index = i;
				n = i + 64 + (i / 64);
			}
		}
	}
	return index;
}

nano::public_key wallet::change_seed (nano::raw_key const & prv_a, uint32_t count)
{
	nano::public_key result;
	{
		auto transaction = wallets.tx_begin_write ();
		result = change_seed_impl (transaction, prv_a, count);
	}
	wallets.refresh_rep_keys_cache ();
	return result;
}

nano::public_key wallet::change_seed_impl (nano::store::write_transaction const & transaction_a, nano::raw_key const & prv_a, uint32_t count)
{
	logger.info (nano::log::type::wallet, "Changing wallet seed");

	store.seed_set (transaction_a, prv_a);
	auto account = deterministic_insert_impl (transaction_a);
	if (count == 0)
	{
		count = deterministic_check_impl (transaction_a, 0);
		logger.info (nano::log::type::wallet, "Auto-detected {} accounts to generate from seed", count);
	}
	for (uint32_t i (0); i < count; ++i)
	{
		// Disable work generation to prevent weak CPU nodes stuck
		account = deterministic_insert_impl (transaction_a, false);
	}

	logger.info (nano::log::type::wallet, "Completed changing wallet seed and generating accounts");

	return account;
}

void wallet::deterministic_restore ()
{
	{
		auto transaction = wallets.tx_begin_write ();
		deterministic_restore_impl (transaction);
	}
	wallets.refresh_rep_keys_cache ();
}

void wallet::deterministic_restore_impl (nano::store::write_transaction const & transaction_a)
{
	auto index (store.deterministic_index_get (transaction_a));
	auto new_index (deterministic_check_impl (transaction_a, index));
	for (uint32_t i (index); i <= new_index && index != new_index; ++i)
	{
		// Disable work generation to prevent weak CPU nodes stuck
		deterministic_insert_impl (transaction_a, false);
	}
}

bool wallet::rekey (std::string const & password_a)
{
	bool result;
	{
		auto transaction = wallets.tx_begin_write ();
		result = store.rekey (transaction, password_a);
	}
	if (!result)
	{
		wallets.refresh_rep_keys_cache ();
	}
	return result;
}

bool wallet::is_locked () const
{
	auto transaction = wallets.tx_begin_read ();
	return !store.valid_password (transaction);
}

void wallet::lock ()
{
	logger.info (nano::log::type::wallet, "Wallet locked");
	nano::raw_key empty;
	empty.clear ();
	store.password.value_set (empty);
	wallets.refresh_rep_keys_cache ();
}

void wallet::remove_account (nano::account const & account_a)
{
	{
		auto transaction = wallets.tx_begin_write ();
		store.erase (transaction, account_a);
	}
	wallets.refresh_rep_keys_cache ();
}

std::vector<nano::account> wallet::accounts () const
{
	auto transaction = wallets.tx_begin_read ();
	return store.accounts (transaction);
}

nano::result<bool> wallet::move_accounts (wallet & source, std::vector<nano::public_key> const & accounts)
{
	nano::result<bool> result{ true };
	{
		auto transaction = wallets.tx_begin_write ();
		result = store.move (transaction, source.store, accounts);
	}
	wallets.refresh_rep_keys_cache ();
	return result;
}

key_type wallet::key_type (nano::account const & account_a) const
{
	auto transaction = wallets.tx_begin_read ();
	auto value = store.entry_get_raw (transaction, account_a);
	return store.key_type (value);
}

void wallet::set_representative (nano::account const & rep_a)
{
	auto transaction = wallets.tx_begin_write ();
	store.representative_set (transaction, rep_a);
}

nano::account wallet::get_representative () const
{
	auto transaction = wallets.tx_begin_read ();
	return store.representative (transaction);
}

nano::result<nano::raw_key> wallet::get_seed () const
{
	auto transaction = wallets.tx_begin_read ();
	if (!store.valid_password (transaction))
	{
		return nano::error (nano::error_common::wallet_locked);
	}
	return store.seed (transaction);
}

uint32_t wallet::get_deterministic_index () const
{
	auto transaction = wallets.tx_begin_read ();
	return store.deterministic_index_get (transaction);
}

nano::result<uint64_t> wallet::get_work (nano::public_key const & pub) const
{
	auto transaction = wallets.tx_begin_read ();
	auto result = store.work_get (transaction, pub);
	if (result)
	{
		return *result;
	}
	return nano::error (nano::error_common::account_not_found_wallet);
}

void wallet::set_work (nano::public_key const & pub_a, uint64_t work_a)
{
	auto transaction = wallets.tx_begin_write ();
	store.work_put (transaction, pub_a, work_a);
}

nano::result<nano::raw_key> wallet::fetch_prv (nano::account const & pub_a) const
{
	auto transaction = wallets.tx_begin_read ();
	return store.fetch (transaction, pub_a);
}

bool wallet::live ()
{
	return store.handle->valid ();
}

std::unordered_set<nano::account> wallet::reps () const
{
	return *representatives.lock ();
}

void wallet::work_cache_blocking (nano::account const & account_a, nano::root const & root_a)
{
	if (wallets.node.work_generation_enabled ())
	{
		auto difficulty (wallets.node.default_difficulty (nano::work_version::work_1));
		auto opt_work_l (wallets.node.work_generate_blocking (nano::work_version::work_1, root_a, difficulty, account_a));
		if (opt_work_l.has_value ())
		{
			auto transaction_l (wallets.tx_begin_write ());
			// No TOCTOU between `live()` and the ops below: LMDB's single-writer rule means
			// `wallets::destroy()` cannot commit (and clear `store.handle`) until `transaction_l`
			// ends, so the handle is stable for the duration of this block.
			if (live () && store.exists (transaction_l, account_a))
			{
				work_update_impl (transaction_l, account_a, root_a, opt_work_l.value ());
			}
		}
		else if (!wallets.node.stopped)
		{
			logger.warn (nano::log::type::wallet, "Could not precache work for root: {} due to work generation failure", root_a);
		}
	}
}

/*
 * wallets
 */

nano::uint128_t const wallets::generate_priority = std::numeric_limits<nano::uint128_t>::max ();
nano::uint128_t const wallets::high_priority = std::numeric_limits<nano::uint128_t>::max () - 1;

wallets::wallets (
nano::node & node_a,
nano::wallet::wallets_backend & backend_a,
nano::ledger & ledger_a,
nano::node_config const & config_a,
nano::network_params const & network_params_a,
nano::online_reps & online_reps_a,
nano::network & network_a,
nano::stats & stats_a,
nano::logger & logger_a) :
	node{ node_a },
	backend{ backend_a },
	ledger{ ledger_a },
	config{ config_a },
	network_params{ network_params_a },
	online_reps{ online_reps_a },
	network{ network_a },
	stats{ stats_a },
	logger{ logger_a },
	observer{ [] (bool) {} },
	kdf{ network_params.kdf_work },
	workers{ config.wallet_threads, nano::thread_role::name::wallet_worker, /* auto_start */ true }
{
	logger.info (nano::log::type::wallet, "Loading wallets from: {}", backend.database_path ().string ());

	nano::unique_lock<nano::mutex> lock{ mutex };
	{
		auto transaction (tx_begin_write ());
		for (auto it = backend.index_begin (transaction), end = backend.index_end (transaction); it != end; ++it)
		{
			// The wallet index range may also include entries for non-wallet sub-tables (e.g. `send_action_ids` on LMDB);
			// skip anything that doesn't parse as a 64-char hex wallet id.
			auto id = try_parse_wallet_id (bytes_to_string (it->first));
			if (!id)
			{
				continue;
			}
			auto text = id->to_string ();
			release_assert (items.find (*id) == items.end ());
			try
			{
				auto wallet_l = std::make_shared<wallet> (transaction, *this, text);
				items[*id] = wallet_l;
			}
			catch (std::exception const & ex)
			{
				logger.error (nano::log::type::wallet, "Failed to open wallet {}: {}", text, ex.what ());
			}
		}
	}

	logger.info (nano::log::type::wallet, "Found {} wallet(s)", items.size ());
	for (auto const & item : items)
	{
		logger.info (nano::log::type::wallet, "Wallet: {}", item.first);
	}

	// Backup before upgrade wallets
	bool backup_required (false);
	if (config.backup_before_upgrade)
	{
		auto transaction (tx_begin_read ());
		for (auto & item : items)
		{
			if (item.second->store.version (transaction) != wallet_store::version_current)
			{
				backup_required = true;
				break;
			}
		}
	}
	if (backup_required)
	{
		backend.backup (logger);
	}
	for (auto & item : items)
	{
		item.second->enter_initial_password ();
	}
}

wallets::~wallets ()
{
	stop ();
}

void wallets::start ()
{
	thread = std::thread{ [this] () {
		nano::thread_role::set (nano::thread_role::name::wallet_actions);
		do_wallet_actions ();
	} };

	if (config.enable_voting)
	{
		reps_thread = std::thread{ [this] () {
			nano::thread_role::set (nano::thread_role::name::wallet_reps);
			run_reps_scan ();
		} };
	}

	if (!node.flags.disable_search_pending)
	{
		receivable_thread = std::thread{ [this] () {
			nano::thread_role::set (nano::thread_role::name::wallet_receivable);
			run_receivable_scan ();
		} };
	}
}

void wallets::stop ()
{
	{
		nano::lock_guard<nano::mutex> action_lock{ action_mutex };
		stopped = true;
		actions.clear ();
	}
	condition.notify_all ();
	reps_condition.notify_all ();
	receivable_condition.notify_all ();

	if (thread.joinable ())
	{
		thread.join ();
	}
	if (reps_thread.joinable ())
	{
		reps_thread.join ();
	}
	if (receivable_thread.joinable ())
	{
		receivable_thread.join ();
	}

	workers.stop ();
}

std::shared_ptr<wallet> wallets::open (nano::wallet_id const & id_a)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	std::shared_ptr<wallet> result;
	auto existing (items.find (id_a));
	if (existing != items.end ())
	{
		result = existing->second;
	}
	return result;
}

std::shared_ptr<wallet> wallets::create (nano::wallet_id const & id_a)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	debug_assert (items.find (id_a) == items.end ());
	{
		auto transaction (tx_begin_write ());
		try
		{
			auto result = std::make_shared<wallet> (transaction, *this, id_a.to_string ());
			debug_assert (result->store.valid_password (transaction));
			items[id_a] = result;
			return result;
		}
		catch (std::exception const & ex)
		{
			logger.error (nano::log::type::wallet, "Failed to create wallet {}: {}", id_a, ex.what ());
		}
	}
	return nullptr;
}

std::shared_ptr<wallet> wallets::create_from_json (nano::wallet_id const & id_a, std::string const & json_a)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	debug_assert (items.find (id_a) == items.end ());
	{
		auto transaction (tx_begin_write ());
		try
		{
			auto result = std::make_shared<wallet> (transaction, *this, id_a.to_string (), json_a);
			items[id_a] = result;
			return result;
		}
		catch (std::exception const & ex)
		{
			logger.error (nano::log::type::wallet, "Failed to create wallet {} from JSON: {}", id_a, ex.what ());
		}
	}
	return nullptr;
}

bool wallets::search_receivable (nano::wallet_id const & wallet_a)
{
	auto result (false);
	if (auto wallet = open (wallet_a); wallet != nullptr)
	{
		result = wallet->search_receivable ();
	}
	return result;
}

void wallets::search_receivable_all ()
{
	auto wallets_l = all_wallets ();
	for (auto const & [id, wallet] : wallets_l)
	{
		wallet->search_receivable ();
	}
}

void wallets::destroy (nano::wallet_id const & id_a)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	auto transaction (tx_begin_write ());
	// action_mutex should be after transactions to prevent deadlocks in deterministic_insert () & insert_adhoc ()
	nano::lock_guard<nano::mutex> action_lock{ action_mutex };
	auto existing (items.find (id_a));
	release_assert (existing != items.end ());
	auto wallet (existing->second);
	items.erase (existing);
	wallet->store.destroy (transaction);
}

void wallets::reload ()
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	auto transaction (tx_begin_write ());
	std::unordered_set<nano::uint256_union> stored_items;
	for (auto it = backend.index_begin (transaction), end = backend.index_end (transaction); it != end; ++it)
	{
		// The wallet index range may also include entries for non-wallet sub-tables (e.g. `send_action_ids` on LMDB);
		// skip anything that doesn't parse as a 64-char hex wallet id.
		auto id = try_parse_wallet_id (bytes_to_string (it->first));
		if (!id)
		{
			continue;
		}
		auto text = id->to_string ();
		// New wallet
		if (items.find (*id) == items.end ())
		{
			try
			{
				auto wallet_l = std::make_shared<wallet> (transaction, *this, text);
				items[*id] = wallet_l;
			}
			catch (std::exception const & ex)
			{
				logger.error (nano::log::type::wallet, "Failed to open wallet {}: {}", text, ex.what ());
			}
		}
		// List of wallets on disk
		stored_items.insert (*id);
	}
	// Delete non existing wallets from memory
	std::vector<nano::wallet_id> deleted_items;
	for (auto i : items)
	{
		if (stored_items.find (i.first) == stored_items.end ())
		{
			deleted_items.push_back (i.first);
		}
	}
	for (auto & i : deleted_items)
	{
		debug_assert (items.find (i) == items.end ());
		items.erase (i);
	}
}

void wallets::queue_wallet_action (nano::uint128_t const & amount_a, std::shared_ptr<wallet> const & wallet_a, std::function<void (wallet &)> action_a)
{
	{
		nano::lock_guard<nano::mutex> action_lock{ action_mutex };
		actions.emplace (amount_a, std::make_pair (wallet_a, action_a));
	}
	condition.notify_all ();
}

bool wallets::exists_impl (nano::store::transaction const & transaction_a, nano::account const & account_a)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	auto result (false);
	for (auto i (items.begin ()), n (items.end ()); !result && i != n; ++i)
	{
		result = i->second->store.exists (transaction_a, account_a);
	}
	return result;
}

bool wallets::exists (nano::account const & account_a)
{
	auto transaction (tx_begin_read ());
	return exists_impl (transaction, account_a);
}

bool wallets::exists_any (nano::account const & account1, nano::account const & account2)
{
	auto transaction (tx_begin_read ());
	return exists_impl (transaction, account1) || exists_impl (transaction, account2);
}

nano::store::write_transaction wallets::tx_begin_write ()
{
	return backend.tx_begin_write ();
}

nano::store::read_transaction wallets::tx_begin_read ()
{
	return backend.tx_begin_read ();
}

void wallets::clear_send_ids ()
{
	auto transaction (tx_begin_write ());
	backend.send_action_ids_clear (transaction);
}

wallet_representatives wallets::reps () const
{
	return *representatives.lock ();
}

auto wallets::signer () -> signer_t
{
	return [this] (auto const & callback) { foreach_representative (callback); };
}

bool wallets::check_rep (nano::account const & account)
{
	auto half_principal_weight = node.minimum_principal_weight () / 2;
	auto representatives_locked = representatives.lock ();
	return check_rep_impl (*representatives_locked, account, half_principal_weight);
}

bool wallets::check_rep_impl (wallet_representatives & reps, nano::account const & account, nano::uint128_t const & half_principal_weight)
{
	auto weight = ledger.weight (account);
	if (weight < config.vote_minimum.number ())
	{
		return false; // account not a representative
	}

	if (weight >= half_principal_weight)
	{
		reps.half_principal = true;
	}

	auto insert_result = reps.accounts.insert (account);
	if (!insert_result.second)
	{
		return false; // account already exists
	}

	++reps.voting;

	return true;
}

void wallets::refresh_reps ()
{
	refresh_rep_index ();
	refresh_rep_keys_cache ();
}

void wallets::refresh_rep_index ()
{
	nano::lock_guard<nano::mutex> guard{ mutex };

	auto reps_locked = representatives.lock ();
	reps_locked->clear ();

	auto const half_principal_weight = node.minimum_principal_weight () / 2;

	auto wallet_txn = tx_begin_read ();

	for (auto const & [id, wallet] : items)
	{
		std::unordered_set<nano::account> new_representatives;
		for (auto i = wallet->store.begin (wallet_txn), n = wallet->store.end (wallet_txn); i != n; ++i)
		{
			auto account = i->first;
			if (check_rep_impl (*reps_locked, account, half_principal_weight))
			{
				new_representatives.insert (account);
			}
		}
		wallet->representatives.lock ()->swap (new_representatives);
	}
}

void wallets::foreach_representative (std::function<void (nano::public_key const & pub, nano::raw_key const & prv)> const & action)
{
	if (config.enable_voting)
	{
		// Cache lock is held during callbacks, recursive calls are not allowed
		auto locked = rep_keys_cache.lock ();
		for (auto const & [pub, fan_ptr] : *locked)
		{
			nano::raw_key prv;
			fan_ptr->value (prv);
			action (pub, prv);
		}
	}
}

void wallets::refresh_rep_keys_cache ()
{
	if (!config.enable_voting)
	{
		return;
	}

	std::vector<std::pair<nano::public_key, std::unique_ptr<nano::fan>>> new_cache;

	auto wallet_txn = tx_begin_read ();

	nano::lock_guard<nano::mutex> lock{ mutex };

	for (auto const & [id, wallet] : items)
	{
		nano::lock_guard<std::recursive_mutex> store_lock{ wallet->store.mutex };

		auto reps_locked = wallet->representatives.lock ();
		for (auto const & account : *reps_locked)
		{
			if (wallet->store.exists (wallet_txn, account))
			{
				if (wallet->store.valid_password (wallet_txn))
				{
					auto prv_result = wallet->store.fetch (wallet_txn, account);
					release_assert (prv_result, "failed to fetch private key for representative account", account.to_account ());

					// Store private key spread across multiple heap allocations via fan to avoid plaintext keys in memory at rest
					new_cache.emplace_back (account, std::make_unique<nano::fan> (prv_result.value (), config.password_fanout));
				}
				else
				{
					static auto last_log = std::chrono::steady_clock::time_point ();
					if (last_log < std::chrono::steady_clock::now () - std::chrono::seconds (60))
					{
						last_log = std::chrono::steady_clock::now ();
						logger.warn (nano::log::type::wallet, "Representative locked inside wallet: {}", id);
					}
				}
			}
		}
	}
	rep_keys_cache.lock ()->swap (new_cache);
}

void wallets::run_reps_scan ()
{
	auto delay = [this] () {
		// Representation drifts quickly on the test network but very slowly on the live network
		return network_params.network.is_dev_network ()
		? 100ms
		: (network_params.network.is_test_network ()
		? std::chrono::milliseconds (nano::test_scan_wallet_reps_delay ())
		: std::chrono::minutes (15));
	};

	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		lock.unlock ();

		stats.inc (nano::stat::type::wallet, nano::stat::detail::loop_reps);

		// Recompute local wallet representatives and refresh cached keys
		refresh_reps ();

		lock.lock ();

		reps_condition.wait_for (lock, delay (), [this] () {
			return stopped.load ();
		});
	}
}

void wallets::run_receivable_scan ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		lock.unlock ();

		stats.inc (nano::stat::type::wallet, nano::stat::detail::loop_receivable);

		// Reload wallets from disk
		reload ();

		// Search pending
		search_receivable_all ();

		lock.lock ();

		receivable_condition.wait_for (lock, network_params.node.search_pending_interval, [this] () {
			return stopped.load ();
		});
	}
}

void wallets::receive_confirmed (nano::block_hash const & hash_a, nano::account const & destination_a)
{
	auto wallets_l = all_wallets ();
	auto wallet_transaction = tx_begin_read ();
	for ([[maybe_unused]] auto const & [id, wallet] : wallets_l)
	{
		if (wallet->store.exists (wallet_transaction, destination_a))
		{
			nano::account representative;
			representative = wallet->store.representative (wallet_transaction);
			auto pending = ledger.any.pending_get (ledger.tx_begin_read (), nano::pending_key (destination_a, hash_a));
			if (pending)
			{
				auto amount (pending->amount.number ());
				wallet->receive_async (hash_a, representative, amount, destination_a, [] (std::shared_ptr<nano::block> const &) {});
			}
			else
			{
				if (!ledger.cemented.block_exists_or_pruned (ledger.tx_begin_read (), hash_a))
				{
					logger.warn (nano::log::type::wallet, "Confirmed block is missing: {}", hash_a);
					debug_assert (false, "confirmed block is missing");
				}
				else
				{
					logger.warn (nano::log::type::wallet, "Block has already been received: {}", hash_a);
				}
			}
		}
	}
}

std::unordered_map<nano::wallet_id, std::shared_ptr<wallet>> wallets::all_wallets ()
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return items;
}

void wallets::do_wallet_actions ()
{
	nano::unique_lock<nano::mutex> action_lock{ action_mutex };
	while (!stopped)
	{
		if (!actions.empty ())
		{
			auto first (actions.begin ());
			auto wallet (first->second.first);
			auto current (std::move (first->second.second));
			actions.erase (first);
			// `wallets::destroy()` takes `action_mutex` before clearing `store.handle`,
			// so while we hold `action_lock` the handle is stable. The subsequent
			// `current(*wallet)` runs with `action_lock` released; anything inside the
			// action that touches `store.handle` must rely on its own synchronization
			// (e.g. LMDB's single-writer rule for `work_cache_blocking`).
			if (wallet->live ())
			{
				action_lock.unlock ();
				observer (true);
				current (*wallet);
				observer (false);
				action_lock.lock ();
			}
		}
		else
		{
			condition.wait (action_lock);
		}
	}
}

nano::container_info wallets::container_info () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };

	nano::container_info info;
	info.put ("items", items.size ());
	info.put ("actions", actions.size ());
	info.put ("rep_keys_cache", rep_keys_cache.lock ()->size ());
	return info;
}
}
