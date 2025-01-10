#include <celerix/crypto_lib/random_pool.hpp>
#include <celerix/lib/blocks.hpp>
#include <celerix/lib/files.hpp>
#include <celerix/lib/threading.hpp>
#include <celerix/lib/utility.hpp>
#include <celerix/lib/work_version.hpp>
#include <celerix/node/confirming_set.hpp>
#include <celerix/node/election.hpp>
#include <celerix/node/node.hpp>
#include <celerix/node/wallet.hpp>
#include <celerix/secure/ledger.hpp>
#include <celerix/secure/ledger_set_any.hpp>
#include <celerix/secure/ledger_set_confirmed.hpp>
#include <celerix/store/lmdb/iterator.hpp>
#include <celerix/store/typed_iterator_templ.hpp>

#include <boost/format.hpp>
#include <boost/polymorphic_cast.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <future>

#include <argon2.h>

template class celerix::store::typed_iterator<celerix::account, celerix::wallet_value>;

celerix::uint256_union celerix::wallet_store::check (store::transaction const & transaction_a)
{
	celerix::wallet_value value (entry_get_raw (transaction_a, celerix::wallet_store::check_special));
	return value.key;
}

celerix::uint256_union celerix::wallet_store::salt (store::transaction const & transaction_a)
{
	celerix::wallet_value value (entry_get_raw (transaction_a, celerix::wallet_store::salt_special));
	return value.key;
}

void celerix::wallet_store::wallet_key (celerix::raw_key & prv_a, store::transaction const & transaction_a)
{
	celerix::lock_guard<std::recursive_mutex> lock{ mutex };
	celerix::raw_key wallet_l;
	wallet_key_mem.value (wallet_l);
	celerix::raw_key password_l;
	password.value (password_l);
	prv_a.decrypt (wallet_l, password_l, salt (transaction_a).owords[0]);
}

void celerix::wallet_store::seed (celerix::raw_key & prv_a, store::transaction const & transaction_a)
{
	celerix::wallet_value value (entry_get_raw (transaction_a, celerix::wallet_store::seed_special));
	celerix::raw_key password_l;
	wallet_key (password_l, transaction_a);
	prv_a.decrypt (value.key, password_l, salt (transaction_a).owords[seed_iv_index]);
}

void celerix::wallet_store::seed_set (store::transaction const & transaction_a, celerix::raw_key const & prv_a)
{
	celerix::raw_key password_l;
	wallet_key (password_l, transaction_a);
	celerix::raw_key ciphertext;
	ciphertext.encrypt (prv_a, password_l, salt (transaction_a).owords[seed_iv_index]);
	entry_put_raw (transaction_a, celerix::wallet_store::seed_special, celerix::wallet_value (ciphertext, 0));
	deterministic_clear (transaction_a);
}

celerix::public_key celerix::wallet_store::deterministic_insert (store::transaction const & transaction_a)
{
	auto index (deterministic_index_get (transaction_a));
	auto prv = deterministic_key (transaction_a, index);
	celerix::public_key result (celerix::pub_key (prv));
	while (exists (transaction_a, result))
	{
		++index;
		prv = deterministic_key (transaction_a, index);
		result = celerix::pub_key (prv);
	}
	uint64_t marker (1);
	marker <<= 32;
	marker |= index;
	entry_put_raw (transaction_a, result, celerix::wallet_value (marker, 0));
	++index;
	deterministic_index_set (transaction_a, index);
	return result;
}

celerix::public_key celerix::wallet_store::deterministic_insert (store::transaction const & transaction_a, uint32_t const index)
{
	auto prv = deterministic_key (transaction_a, index);
	celerix::public_key result (celerix::pub_key (prv));
	uint64_t marker (1);
	marker <<= 32;
	marker |= index;
	entry_put_raw (transaction_a, result, celerix::wallet_value (marker, 0));
	return result;
}

celerix::raw_key celerix::wallet_store::deterministic_key (store::transaction const & transaction_a, uint32_t index_a)
{
	debug_assert (valid_password (transaction_a));
	celerix::raw_key seed_l;
	seed (seed_l, transaction_a);
	return celerix::deterministic_key (seed_l, index_a);
}

uint32_t celerix::wallet_store::deterministic_index_get (store::transaction const & transaction_a)
{
	celerix::wallet_value value (entry_get_raw (transaction_a, celerix::wallet_store::deterministic_index_special));
	return static_cast<uint32_t> (value.key.number () & static_cast<uint32_t> (-1));
}

void celerix::wallet_store::deterministic_index_set (store::transaction const & transaction_a, uint32_t index_a)
{
	celerix::raw_key index_l (index_a);
	celerix::wallet_value value (index_l, 0);
	entry_put_raw (transaction_a, celerix::wallet_store::deterministic_index_special, value);
}

void celerix::wallet_store::deterministic_clear (store::transaction const & transaction_a)
{
	celerix::uint256_union key (0);
	for (auto i (begin (transaction_a)), n (end (transaction_a)); i != n;)
	{
		switch (key_type (celerix::wallet_value (i->second)))
		{
			case celerix::key_type::deterministic:
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

bool celerix::wallet_store::valid_password (store::transaction const & transaction_a)
{
	celerix::raw_key zero;
	zero.clear ();
	celerix::raw_key wallet_key_l;
	wallet_key (wallet_key_l, transaction_a);
	celerix::uint256_union check_l;
	check_l.encrypt (zero, wallet_key_l, salt (transaction_a).owords[check_iv_index]);
	bool ok = check (transaction_a) == check_l;
	return ok;
}

bool celerix::wallet_store::attempt_password (store::transaction const & transaction_a, std::string const & password_a)
{
	bool result = false;
	{
		celerix::lock_guard<std::recursive_mutex> lock{ mutex };
		celerix::raw_key password_l;
		derive_key (password_l, transaction_a, password_a);
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

bool celerix::wallet_store::rekey (store::transaction const & transaction_a, std::string const & password_a)
{
	celerix::lock_guard<std::recursive_mutex> lock{ mutex };
	bool result (false);
	if (valid_password (transaction_a))
	{
		celerix::raw_key password_new;
		derive_key (password_new, transaction_a, password_a);
		celerix::raw_key wallet_key_l;
		wallet_key (wallet_key_l, transaction_a);
		celerix::raw_key password_l;
		password.value (password_l);
		password.value_set (password_new);
		celerix::raw_key encrypted;
		encrypted.encrypt (wallet_key_l, password_new, salt (transaction_a).owords[0]);
		celerix::raw_key wallet_enc;
		wallet_enc = encrypted;
		wallet_key_mem.value_set (wallet_enc);
		entry_put_raw (transaction_a, celerix::wallet_store::wallet_key_special, celerix::wallet_value (encrypted, 0));
	}
	else
	{
		result = true;
	}
	return result;
}

void celerix::wallet_store::derive_key (celerix::raw_key & prv_a, store::transaction const & transaction_a, std::string const & password_a)
{
	auto salt_l (salt (transaction_a));
	kdf.phs (prv_a, password_a, salt_l);
}

celerix::fan::fan (celerix::raw_key const & key, std::size_t count_a)
{
	auto first (std::make_unique<celerix::raw_key> (key));
	for (auto i (1); i < count_a; ++i)
	{
		auto entry (std::make_unique<celerix::raw_key> ());
		celerix::random_pool::generate_block (entry->bytes.data (), entry->bytes.size ());
		*first ^= *entry;
		values.push_back (std::move (entry));
	}
	values.push_back (std::move (first));
}

void celerix::fan::value (celerix::raw_key & prv_a)
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	value_get (prv_a);
}

void celerix::fan::value_get (celerix::raw_key & prv_a)
{
	debug_assert (!mutex.try_lock ());
	prv_a.clear ();
	for (auto & i : values)
	{
		prv_a ^= *i;
	}
}

void celerix::fan::value_set (celerix::raw_key const & value_a)
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	celerix::raw_key value_l;
	value_get (value_l);
	*(values[0]) ^= value_l;
	*(values[0]) ^= value_a;
}

/*
 * wallet_store
 */

// Wallet version number
celerix::account const celerix::wallet_store::version_special{};
// Random number used to salt private key encryption
celerix::account const celerix::wallet_store::salt_special (1);
// Key used to encrypt wallet keys, encrypted itself by the user password
celerix::account const celerix::wallet_store::wallet_key_special (2);
// Check value used to see if password is valid
celerix::account const celerix::wallet_store::check_special (3);
// Representative account to be used if we open a new account
celerix::account const celerix::wallet_store::representative_special (4);
// Wallet seed for deterministic key generation
celerix::account const celerix::wallet_store::seed_special (5);
// Current key index for deterministic keys
celerix::account const celerix::wallet_store::deterministic_index_special (6);
int const celerix::wallet_store::special_count (7);
std::size_t const celerix::wallet_store::check_iv_index (0);
std::size_t const celerix::wallet_store::seed_iv_index (1);

celerix::wallet_store::wallet_store (bool & init_a, celerix::kdf & kdf_a, store::transaction & transaction_a, store::lmdb::env & env_a, celerix::account representative_a, unsigned fanout_a, std::string const & wallet_a, std::string const & json_a) :
	password (0, fanout_a),
	wallet_key_mem (0, fanout_a),
	kdf (kdf_a),
	env{ env_a }
{
	init_a = false;
	initialize (transaction_a, init_a, wallet_a);
	if (!init_a)
	{
		MDB_val junk;
		debug_assert (mdb_get (env.tx (transaction_a), handle, celerix::store::lmdb::db_val (version_special), &junk) == MDB_NOTFOUND);
		boost::property_tree::ptree wallet_l;
		std::stringstream istream (json_a);
		try
		{
			boost::property_tree::read_json (istream, wallet_l);
		}
		catch (...)
		{
			init_a = true;
		}
		for (auto i (wallet_l.begin ()), n (wallet_l.end ()); i != n; ++i)
		{
			celerix::account key;
			init_a = key.decode_hex (i->first);
			if (!init_a)
			{
				celerix::raw_key value;
				init_a = value.decode_hex (wallet_l.get<std::string> (i->first));
				if (!init_a)
				{
					entry_put_raw (transaction_a, key, celerix::wallet_value (value, 0));
				}
				else
				{
					init_a = true;
				}
			}
			else
			{
				init_a = true;
			}
		}
		init_a |= mdb_get (env.tx (transaction_a), handle, celerix::store::lmdb::db_val (version_special), &junk) != 0;
		init_a |= mdb_get (env.tx (transaction_a), handle, celerix::store::lmdb::db_val (wallet_key_special), &junk) != 0;
		init_a |= mdb_get (env.tx (transaction_a), handle, celerix::store::lmdb::db_val (salt_special), &junk) != 0;
		init_a |= mdb_get (env.tx (transaction_a), handle, celerix::store::lmdb::db_val (check_special), &junk) != 0;
		init_a |= mdb_get (env.tx (transaction_a), handle, celerix::store::lmdb::db_val (representative_special), &junk) != 0;
		celerix::raw_key key;
		key.clear ();
		password.value_set (key);
		key = entry_get_raw (transaction_a, celerix::wallet_store::wallet_key_special).key;
		wallet_key_mem.value_set (key);
	}
}

celerix::wallet_store::wallet_store (bool & init_a, celerix::kdf & kdf_a, store::transaction & transaction_a, store::lmdb::env & env_a, celerix::account representative_a, unsigned fanout_a, std::string const & wallet_a) :
	password (0, fanout_a),
	wallet_key_mem (0, fanout_a),
	kdf (kdf_a),
	env{ env_a }
{
	init_a = false;
	initialize (transaction_a, init_a, wallet_a);
	if (!init_a)
	{
		int version_status;
		MDB_val version_value;
		version_status = mdb_get (env.tx (transaction_a), handle, celerix::store::lmdb::db_val (version_special), &version_value);
		if (version_status == MDB_NOTFOUND)
		{
			version_put (transaction_a, version_current);
			celerix::raw_key salt_l;
			random_pool::generate_block (salt_l.bytes.data (), salt_l.bytes.size ());
			entry_put_raw (transaction_a, celerix::wallet_store::salt_special, celerix::wallet_value (salt_l, 0));
			// Wallet key is a fixed random key that encrypts all entries
			celerix::raw_key wallet_key;
			random_pool::generate_block (wallet_key.bytes.data (), sizeof (wallet_key.bytes));
			celerix::raw_key password_l;
			password_l.clear ();
			password.value_set (password_l);
			celerix::raw_key zero;
			zero.clear ();
			// Wallet key is encrypted by the user's password
			celerix::raw_key encrypted;
			encrypted.encrypt (wallet_key, zero, salt_l.owords[0]);
			entry_put_raw (transaction_a, celerix::wallet_store::wallet_key_special, celerix::wallet_value (encrypted, 0));
			celerix::raw_key wallet_key_enc;
			wallet_key_enc = encrypted;
			wallet_key_mem.value_set (wallet_key_enc);
			celerix::raw_key check;
			check.encrypt (zero, wallet_key, salt_l.owords[check_iv_index]);
			entry_put_raw (transaction_a, celerix::wallet_store::check_special, celerix::wallet_value (check, 0));
			celerix::raw_key rep;
			rep.bytes = representative_a.bytes;
			entry_put_raw (transaction_a, celerix::wallet_store::representative_special, celerix::wallet_value (rep, 0));
			celerix::raw_key seed;
			random_pool::generate_block (seed.bytes.data (), seed.bytes.size ());
			seed_set (transaction_a, seed);
			entry_put_raw (transaction_a, celerix::wallet_store::deterministic_index_special, celerix::wallet_value (0, 0));
		}
	}
	celerix::raw_key key;
	key = entry_get_raw (transaction_a, celerix::wallet_store::wallet_key_special).key;
	wallet_key_mem.value_set (key);
}

std::vector<celerix::account> celerix::wallet_store::accounts (store::transaction const & transaction_a)
{
	std::vector<celerix::account> result;
	for (auto i (begin (transaction_a)), n (end (transaction_a)); i != n; ++i)
	{
		celerix::account const & account (i->first);
		result.push_back (account);
	}
	return result;
}

void celerix::wallet_store::initialize (store::transaction const & transaction_a, bool & init_a, std::string const & path_a)
{
	debug_assert (strlen (path_a.c_str ()) == path_a.size ());
	auto error (0);
	MDB_dbi handle_l;
	error |= mdb_dbi_open (env.tx (transaction_a), path_a.c_str (), MDB_CREATE, &handle_l);
	handle = handle_l;
	init_a = error != 0;
}

bool celerix::wallet_store::is_representative (store::transaction const & transaction_a)
{
	return exists (transaction_a, representative (transaction_a));
}

void celerix::wallet_store::representative_set (store::transaction const & transaction_a, celerix::account const & representative_a)
{
	celerix::raw_key rep;
	rep.bytes = representative_a.bytes;
	entry_put_raw (transaction_a, celerix::wallet_store::representative_special, celerix::wallet_value (rep, 0));
}

celerix::account celerix::wallet_store::representative (store::transaction const & transaction_a)
{
	celerix::wallet_value value (entry_get_raw (transaction_a, celerix::wallet_store::representative_special));
	return reinterpret_cast<celerix::account const &> (value.key);
}

celerix::public_key celerix::wallet_store::insert_adhoc (store::transaction const & transaction_a, celerix::raw_key const & prv)
{
	debug_assert (valid_password (transaction_a));
	celerix::public_key pub (celerix::pub_key (prv));
	celerix::raw_key password_l;
	wallet_key (password_l, transaction_a);
	celerix::raw_key ciphertext;
	ciphertext.encrypt (prv, password_l, pub.owords[0].number ());
	entry_put_raw (transaction_a, pub, celerix::wallet_value (ciphertext, 0));
	return pub;
}

bool celerix::wallet_store::insert_watch (store::transaction const & transaction_a, celerix::account const & pub_a)
{
	bool error (!valid_public_key (pub_a));
	if (!error)
	{
		entry_put_raw (transaction_a, pub_a, celerix::wallet_value (celerix::raw_key (0), 0));
	}
	return error;
}

void celerix::wallet_store::erase (store::transaction const & transaction_a, celerix::account const & pub)
{
	auto status (mdb_del (env.tx (transaction_a), handle, celerix::store::lmdb::db_val (pub), nullptr));
	(void)status;
	debug_assert (status == 0);
}

celerix::wallet_value celerix::wallet_store::entry_get_raw (store::transaction const & transaction_a, celerix::account const & pub_a)
{
	celerix::wallet_value result;
	celerix::store::lmdb::db_val value;
	auto status (mdb_get (env.tx (transaction_a), handle, celerix::store::lmdb::db_val (pub_a), value));
	if (status == 0)
	{
		result = celerix::wallet_value (value);
	}
	else
	{
		result.key.clear ();
		result.work = 0;
	}
	return result;
}

void celerix::wallet_store::entry_put_raw (store::transaction const & transaction_a, celerix::account const & pub_a, celerix::wallet_value const & entry_a)
{
	auto status (mdb_put (env.tx (transaction_a), handle, celerix::store::lmdb::db_val (pub_a), celerix::store::lmdb::db_val (sizeof (entry_a), const_cast<celerix::wallet_value *> (&entry_a)), 0));
	(void)status;
	debug_assert (status == 0);
}

celerix::key_type celerix::wallet_store::key_type (celerix::wallet_value const & value_a)
{
	auto number (value_a.key.number ());
	celerix::key_type result;
	auto text (number.convert_to<std::string> ());
	if (number > std::numeric_limits<uint64_t>::max ())
	{
		result = celerix::key_type::adhoc;
	}
	else
	{
		if ((number >> 32).convert_to<uint32_t> () == 1)
		{
			result = celerix::key_type::deterministic;
		}
		else
		{
			result = celerix::key_type::unknown;
		}
	}
	return result;
}

bool celerix::wallet_store::fetch (store::transaction const & transaction_a, celerix::account const & pub, celerix::raw_key & prv)
{
	auto result (false);
	if (valid_password (transaction_a))
	{
		celerix::wallet_value value (entry_get_raw (transaction_a, pub));
		if (!value.key.is_zero ())
		{
			switch (key_type (value))
			{
				case celerix::key_type::deterministic:
				{
					celerix::raw_key seed_l;
					seed (seed_l, transaction_a);
					uint32_t index (static_cast<uint32_t> (value.key.number () & static_cast<uint32_t> (-1)));
					prv = deterministic_key (transaction_a, index);
					break;
				}
				case celerix::key_type::adhoc:
				{
					// Ad-hoc keys
					celerix::raw_key password_l;
					wallet_key (password_l, transaction_a);
					prv.decrypt (value.key, password_l, pub.owords[0].number ());
					break;
				}
				default:
				{
					result = true;
					break;
				}
			}
		}
		else
		{
			result = true;
		}
	}
	else
	{
		result = true;
	}
	if (!result)
	{
		celerix::public_key compare (celerix::pub_key (prv));
		if (!(pub == compare))
		{
			result = true;
		}
	}
	return result;
}

bool celerix::wallet_store::valid_public_key (celerix::public_key const & pub)
{
	return pub.number () >= special_count;
}

bool celerix::wallet_store::exists (store::transaction const & transaction_a, celerix::public_key const & pub)
{
	return valid_public_key (pub) && find (transaction_a, pub) != end (transaction_a);
}

void celerix::wallet_store::serialize_json (store::transaction const & transaction_a, std::string & string_a)
{
	boost::property_tree::ptree tree;
	using iterator = store::typed_iterator<celerix::uint256_union, celerix::wallet_value>;
	for (iterator i{ store::iterator{ store::lmdb::iterator::begin (env.tx (transaction_a), handle) } }, n{ store::iterator{ store::lmdb::iterator::end (env.tx (transaction_a), handle) } }; i != n; ++i)
	{
		tree.put (i->first.to_string (), i->second.key.to_string ());
	}
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree);
	string_a = ostream.str ();
}

void celerix::wallet_store::write_backup (store::transaction const & transaction_a, std::filesystem::path const & path_a)
{
	std::ofstream backup_file;
	backup_file.open (path_a.string ());
	if (!backup_file.fail ())
	{
		// Set permissions to 600
		boost::system::error_code ec;
		celerix::set_secure_perm_file (path_a, ec);

		std::string json;
		serialize_json (transaction_a, json);
		backup_file << json;
	}
}

bool celerix::wallet_store::move (store::transaction const & transaction_a, celerix::wallet_store & other_a, std::vector<celerix::public_key> const & keys)
{
	debug_assert (valid_password (transaction_a));
	debug_assert (other_a.valid_password (transaction_a));
	auto result (false);
	for (auto i (keys.begin ()), n (keys.end ()); i != n; ++i)
	{
		celerix::raw_key prv;
		auto error (other_a.fetch (transaction_a, *i, prv));
		result = result | error;
		if (!result)
		{
			insert_adhoc (transaction_a, prv);
			other_a.erase (transaction_a, *i);
		}
	}
	return result;
}

bool celerix::wallet_store::import (store::transaction const & transaction_a, celerix::wallet_store & other_a)
{
	debug_assert (valid_password (transaction_a));
	debug_assert (other_a.valid_password (transaction_a));
	auto result (false);
	for (auto i (other_a.begin (transaction_a)), n (end (transaction_a)); i != n; ++i)
	{
		celerix::raw_key prv;
		auto error (other_a.fetch (transaction_a, i->first, prv));
		result = result | error;
		if (!result)
		{
			if (!prv.is_zero ())
			{
				insert_adhoc (transaction_a, prv);
			}
			else
			{
				insert_watch (transaction_a, i->first);
			}
			other_a.erase (transaction_a, i->first);
		}
	}
	return result;
}

bool celerix::wallet_store::work_get (store::transaction const & transaction_a, celerix::public_key const & pub_a, uint64_t & work_a)
{
	auto result (false);
	auto entry (entry_get_raw (transaction_a, pub_a));
	if (!entry.key.is_zero ())
	{
		work_a = entry.work;
	}
	else
	{
		result = true;
	}
	return result;
}

void celerix::wallet_store::work_put (store::transaction const & transaction_a, celerix::public_key const & pub_a, uint64_t work_a)
{
	auto entry (entry_get_raw (transaction_a, pub_a));
	debug_assert (!entry.key.is_zero ());
	entry.work = work_a;
	entry_put_raw (transaction_a, pub_a, entry);
}

unsigned celerix::wallet_store::version (store::transaction const & transaction_a)
{
	celerix::wallet_value value (entry_get_raw (transaction_a, celerix::wallet_store::version_special));
	auto entry (value.key);
	auto result (static_cast<unsigned> (entry.bytes[31]));
	return result;
}

void celerix::wallet_store::version_put (store::transaction const & transaction_a, unsigned version_a)
{
	celerix::raw_key entry (version_a);
	entry_put_raw (transaction_a, celerix::wallet_store::version_special, celerix::wallet_value (entry, 0));
}

void celerix::kdf::phs (celerix::raw_key & result_a, std::string const & password_a, celerix::uint256_union const & salt_a)
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	auto success (argon2_hash (1, kdf_work, 1, password_a.data (), password_a.size (), salt_a.bytes.data (), salt_a.bytes.size (), result_a.bytes.data (), result_a.bytes.size (), NULL, 0, Argon2_d, 0x10));
	debug_assert (success == 0);
	(void)success;
}

/*
 * wallet
 */

celerix::wallet::wallet (bool & init_a, store::transaction & transaction_a, celerix::wallets & wallets_a, std::string const & wallet_a) :
	lock_observer ([] (bool, bool) {}),
	store (init_a, wallets_a.kdf, transaction_a, wallets_a.env, wallets_a.node.config.random_representative (), wallets_a.node.config.password_fanout, wallet_a),
	wallets (wallets_a),
	logger (wallets_a.logger)
{
}

celerix::wallet::wallet (bool & init_a, store::transaction & transaction_a, celerix::wallets & wallets_a, std::string const & wallet_a, std::string const & json) :
	lock_observer ([] (bool, bool) {}),
	store (init_a, wallets_a.kdf, transaction_a, wallets_a.env, wallets_a.node.config.random_representative (), wallets_a.node.config.password_fanout, wallet_a, json),
	wallets (wallets_a),
	logger (wallets_a.logger)
{
}

void celerix::wallet::enter_initial_password ()
{
	celerix::raw_key password_l;
	{
		celerix::lock_guard<std::recursive_mutex> lock{ store.mutex };
		store.password.value (password_l);
	}
	if (password_l.is_zero ())
	{
		auto transaction (wallets.tx_begin_write ());
		if (store.valid_password (transaction))
		{
			// Newly created wallets have a zero key
			store.rekey (transaction, "");
		}
		else
		{
			enter_password (transaction, "");
		}
	}
}

bool celerix::wallet::enter_password (store::transaction const & transaction_a, std::string const & password_a)
{
	auto result (store.attempt_password (transaction_a, password_a));
	if (!result)
	{
		logger.info (celerix::log::type::wallet, "Wallet unlocked");

		auto this_l = shared_from_this ();
		wallets.queue_wallet_action (celerix::wallets::high_priority, this_l, [this_l] (celerix::wallet & wallet) {
			// Wallets must survive node lifetime
			this_l->search_receivable (this_l->wallets.tx_begin_read ());
		});
	}
	else
	{
		logger.warn (celerix::log::type::wallet, "Invalid password, wallet locked");
	}
	lock_observer (result, password_a.empty ());
	return result;
}

celerix::public_key celerix::wallet::deterministic_insert (store::transaction const & transaction_a, bool generate_work_a)
{
	celerix::public_key key{};
	if (store.valid_password (transaction_a))
	{
		key = store.deterministic_insert (transaction_a);

		logger.info (celerix::log::type::wallet, "Deterministically inserted new account: {}", key.to_account ());

		if (generate_work_a)
		{
			work_ensure (key, key);
		}
		auto half_principal_weight (wallets.node.minimum_principal_weight () / 2);
		if (wallets.check_rep (key, half_principal_weight))
		{
			logger.info (celerix::log::type::wallet, "New account qualified as a representative: {}", key.to_account ());

			celerix::lock_guard<celerix::mutex> lock{ representatives_mutex };
			representatives.insert (key);
		}
	}
	return key;
}

celerix::public_key celerix::wallet::deterministic_insert (uint32_t const index, bool generate_work_a)
{
	auto transaction (wallets.tx_begin_write ());
	celerix::public_key key{};
	if (store.valid_password (transaction))
	{
		key = store.deterministic_insert (transaction, index);

		logger.info (celerix::log::type::wallet, "Deterministically inserted new account: {}", key.to_account ());

		if (generate_work_a)
		{
			work_ensure (key, key);
		}
	}
	return key;
}

celerix::public_key celerix::wallet::deterministic_insert (bool generate_work_a)
{
	auto transaction (wallets.tx_begin_write ());
	auto result (deterministic_insert (transaction, generate_work_a));
	return result;
}

celerix::public_key celerix::wallet::insert_adhoc (celerix::raw_key const & key_a, bool generate_work_a)
{
	celerix::public_key key{};
	auto transaction (wallets.tx_begin_write ());
	if (store.valid_password (transaction))
	{
		key = store.insert_adhoc (transaction, key_a);
		auto block_transaction = wallets.node.ledger.tx_begin_read ();
		if (generate_work_a)
		{
			work_ensure (key, wallets.node.ledger.latest_root (block_transaction, key));
		}
		auto half_principal_weight (wallets.node.minimum_principal_weight () / 2);
		// Makes sure that the representatives container will
		// be in sync with any added keys.
		transaction.commit ();
		if (wallets.check_rep (key, half_principal_weight))
		{
			celerix::lock_guard<celerix::mutex> lock{ representatives_mutex };
			representatives.insert (key);
		}
	}
	return key;
}

bool celerix::wallet::insert_watch (store::transaction const & transaction_a, celerix::public_key const & pub_a)
{
	return store.insert_watch (transaction_a, pub_a);
}

bool celerix::wallet::exists (celerix::public_key const & account_a)
{
	auto transaction (wallets.tx_begin_read ());
	return store.exists (transaction, account_a);
}

bool celerix::wallet::import (std::string const & json_a, std::string const & password_a)
{
	auto error (false);
	std::unique_ptr<celerix::wallet_store> temp;
	{
		auto transaction (wallets.tx_begin_write ());
		celerix::uint256_union id;
		random_pool::generate_block (id.bytes.data (), id.bytes.size ());
		temp = std::make_unique<celerix::wallet_store> (error, wallets.node.wallets.kdf, transaction, wallets.env, 0, 1, id.to_string (), json_a);
	}
	if (!error)
	{
		auto transaction (wallets.tx_begin_write ());
		error = temp->attempt_password (transaction, password_a);
	}
	auto transaction (wallets.tx_begin_write ());
	if (!error)
	{
		error = store.import (transaction, *temp);
	}
	temp->destroy (transaction);
	return error;
}

void celerix::wallet::serialize (std::string & json_a)
{
	auto transaction (wallets.tx_begin_read ());
	store.serialize_json (transaction, json_a);
}

void celerix::wallet_store::destroy (store::transaction const & transaction_a)
{
	auto status (mdb_drop (env.tx (transaction_a), handle, 1));
	(void)status;
	debug_assert (status == 0);
	handle = 0;
}

std::shared_ptr<celerix::block> celerix::wallet::receive_action (celerix::block_hash const & send_hash_a, celerix::account const & representative_a, celerix::uint128_union const & amount_a, celerix::account const & account_a, uint64_t work_a, bool generate_work_a)
{
	std::shared_ptr<celerix::block> block;
	celerix::block_details details;
	details.is_receive = true;
	if (wallets.node.config.receive_minimum.number () <= amount_a.number ())
	{
		auto block_transaction = wallets.node.ledger.tx_begin_read ();
		auto transaction (wallets.tx_begin_read ());
		if (wallets.node.ledger.any.block_exists_or_pruned (block_transaction, send_hash_a))
		{
			auto pending_info = wallets.node.ledger.any.pending_get (block_transaction, celerix::pending_key (account_a, send_hash_a));
			if (pending_info)
			{
				celerix::raw_key prv;
				if (!store.fetch (transaction, account_a, prv))
				{
					logger.info (celerix::log::type::wallet, "Receiving block {} from account {}, amount: {}",
					send_hash_a.to_string (),
					account_a.to_account (),
					pending_info->amount.number ().convert_to<std::string> ());

					if (work_a == 0)
					{
						store.work_get (transaction, account_a, work_a);
					}
					auto info = wallets.node.ledger.any.account_get (block_transaction, account_a);
					if (info)
					{
						block = std::make_shared<celerix::state_block> (account_a, info->head, info->representative, info->balance.number () + pending_info->amount.number (), send_hash_a, prv, account_a, work_a);
						details.epoch = std::max (info->epoch (), pending_info->epoch);
					}
					else
					{
						block = std::make_shared<celerix::state_block> (account_a, 0, representative_a, pending_info->amount, reinterpret_cast<celerix::link const &> (send_hash_a), prv, account_a, work_a);
						details.epoch = pending_info->epoch;
					}
				}
				else
				{
					logger.warn (celerix::log::type::wallet, "Unable to receive, wallet locked, block {} to account: {}",
					send_hash_a.to_string (),
					account_a.to_account ());
				}
			}
			else
			{
				// Ledger doesn't have this marked as available to receive anymore
				logger.warn (celerix::log::type::wallet, "Not receiving block {}, block already received", send_hash_a.to_string ());
			}
		}
		else
		{
			// Ledger doesn't have this block anymore.
			logger.warn (celerix::log::type::wallet, "Not receiving block {}, block no longer exists or pruned", send_hash_a.to_string ());
		}
	}
	else
	{
		// Someone sent us something below the threshold of receiving
		logger.warn (celerix::log::type::wallet, "Not receiving block {} due to minimum receive threshold", send_hash_a.to_string ());
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

std::shared_ptr<celerix::block> celerix::wallet::change_action (celerix::account const & source_a, celerix::account const & representative_a, uint64_t work_a, bool generate_work_a)
{
	std::shared_ptr<celerix::block> block;
	celerix::block_details details;
	{
		auto transaction (wallets.tx_begin_read ());
		auto block_transaction = wallets.node.ledger.tx_begin_read ();
		if (store.valid_password (transaction))
		{
			auto existing (store.find (transaction, source_a));
			if (existing != store.end (transaction) && !wallets.node.ledger.any.account_head (block_transaction, source_a).is_zero ())
			{
				logger.info (celerix::log::type::wallet, "Changing representative for account {} to {}",
				source_a.to_account (),
				representative_a.to_account ());

				auto info = wallets.node.ledger.any.account_get (block_transaction, source_a);
				debug_assert (info);
				celerix::raw_key prv;
				auto error2 (store.fetch (transaction, source_a, prv));
				(void)error2;
				debug_assert (!error2);
				if (work_a == 0)
				{
					store.work_get (transaction, source_a, work_a);
				}
				block = std::make_shared<celerix::state_block> (source_a, info->head, representative_a, info->balance, 0, prv, source_a, work_a);
				details.epoch = info->epoch ();
			}
			else
			{
				logger.warn (celerix::log::type::wallet, "Changing representative for account {} failed, wallet locked or account not found",
				source_a.to_account ());
			}
		}
		else
		{
			logger.warn (celerix::log::type::wallet, "Changing representative for account {} failed, wallet locked",
			source_a.to_account ());
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

std::shared_ptr<celerix::block> celerix::wallet::send_action (celerix::account const & source_a, celerix::account const & account_a, celerix::uint128_t const & amount_a, uint64_t work_a, bool generate_work_a, boost::optional<std::string> id_a)
{
	boost::optional<celerix::store::lmdb::db_val> id_mdb_val;
	if (id_a)
	{
		id_mdb_val = celerix::store::lmdb::db_val (id_a->size (), const_cast<char *> (id_a->data ()));
	}

	auto prepare_send = [this, &id_mdb_val, &wallets = this->wallets, &store = this->store, &source_a, &amount_a, &work_a, &account_a, &id_a] (auto const & transaction) {
		auto block_transaction = wallets.node.ledger.tx_begin_read ();
		auto error (false);
		auto cached_block (false);
		std::shared_ptr<celerix::block> block;
		celerix::block_details details;
		details.is_send = true;
		if (id_mdb_val)
		{
			celerix::store::lmdb::db_val result;
			auto status (mdb_get (wallets.env.tx (transaction), wallets.node.wallets.send_action_ids, *id_mdb_val, result));
			if (status == 0)
			{
				celerix::block_hash hash (result);
				block = wallets.node.ledger.any.block_get (block_transaction, hash);
				if (block != nullptr)
				{
					logger.warn (celerix::log::type::wallet, "Block already exists for send action with id: {}, existing hash: {}",
					id_a.value (),
					hash.to_string ());

					cached_block = true;
					wallets.node.network.flood_block (block, celerix::transport::traffic_type::block_broadcast_initial);
				}
				else
				{
					logger.warn (celerix::log::type::wallet, "Block was not found in ledger for send action with id: {}, hash: {}",
					id_a.value (),
					hash.to_string ());
				}
			}
			else if (status != MDB_NOTFOUND)
			{
				error = true;
			}
		}
		if (!error && block == nullptr)
		{
			if (store.valid_password (transaction))
			{
				auto existing (store.find (transaction, source_a));
				if (existing != store.end (transaction))
				{
					auto balance (wallets.node.ledger.any.account_balance (block_transaction, source_a));
					if (balance && balance.value ().number () >= amount_a)
					{
						logger.info (celerix::log::type::wallet, "Sending from account: {} to: {}, amount: {}",
						source_a.to_account (),
						account_a.to_account (),
						amount_a.convert_to<std::string> ());

						auto info = wallets.node.ledger.any.account_get (block_transaction, source_a);
						debug_assert (info);
						celerix::raw_key prv;
						auto error2 (store.fetch (transaction, source_a, prv));
						(void)error2;
						debug_assert (!error2);
						if (work_a == 0)
						{
							store.work_get (transaction, source_a, work_a);
						}
						block = std::make_shared<celerix::state_block> (source_a, info->head, info->representative, balance.value ().number () - amount_a, account_a, prv, source_a, work_a);
						details.epoch = info->epoch ();
						if (id_mdb_val && block != nullptr)
						{
							auto status (mdb_put (wallets.env.tx (transaction), wallets.node.wallets.send_action_ids, *id_mdb_val, celerix::store::lmdb::db_val (block->hash ()), 0));
							if (status != 0)
							{
								block = nullptr;
								error = true;
							}
						}
					}
					else
					{
						logger.warn (celerix::log::type::wallet, "Insufficient balance for send from: {}, required: {} but available: {}",
						account_a.to_account (),
						amount_a.convert_to<std::string> (),
						balance ? balance.value ().number ().convert_to<std::string> () : "unknown");
					}
				}
			}
		}
		return std::make_tuple (block, error, cached_block, details);
	};

	std::tuple<std::shared_ptr<celerix::block>, bool, bool, celerix::block_details> result;
	{
		if (id_mdb_val)
		{
			result = prepare_send (wallets.tx_begin_write ());
		}
		else
		{
			result = prepare_send (wallets.tx_begin_read ());
		}
	}

	std::shared_ptr<celerix::block> block;
	bool error;
	bool cached_block;
	celerix::block_details details;
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

bool celerix::wallet::action_complete (std::shared_ptr<celerix::block> const & block_a, celerix::account const & account_a, bool const generate_work_a, celerix::block_details const & details_a)
{
	bool error{ false };
	// Unschedule any work caching for this account
	wallets.delayed_work->erase (account_a);
	if (block_a != nullptr)
	{
		auto required_difficulty{ wallets.node.network_params.work.threshold (block_a->work_version (), details_a) };
		if (wallets.node.network_params.work.difficulty (*block_a) < required_difficulty)
		{
			logger.info (celerix::log::type::wallet, "Cached or provided work for block {} account {} is invalid, regenerating...",
			block_a->hash ().to_string (),
			account_a.to_account ());

			debug_assert (required_difficulty <= wallets.node.max_work_generate_difficulty (block_a->work_version ()));
			error = !wallets.node.work_generate_blocking (*block_a, required_difficulty).has_value ();
		}
		if (!error)
		{
			auto result = wallets.node.process_local (block_a);
			error = !result || result.value () != celerix::block_status::progress;
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

bool celerix::wallet::change_sync (celerix::account const & source_a, celerix::account const & representative_a)
{
	std::promise<bool> result;
	std::future<bool> future = result.get_future ();
	change_async (
	source_a, representative_a, [&result] (std::shared_ptr<celerix::block> const & block_a) {
		result.set_value (block_a == nullptr);
	},
	true);
	return future.get ();
}

void celerix::wallet::change_async (celerix::account const & source_a, celerix::account const & representative_a, std::function<void (std::shared_ptr<celerix::block> const &)> const & action_a, uint64_t work_a, bool generate_work_a)
{
	auto this_l (shared_from_this ());
	wallets.node.wallets.queue_wallet_action (celerix::wallets::high_priority, this_l, [this_l, source_a, representative_a, action_a, work_a, generate_work_a] (celerix::wallet & wallet_a) {
		auto block (wallet_a.change_action (source_a, representative_a, work_a, generate_work_a));
		action_a (block);
	});
}

bool celerix::wallet::receive_sync (std::shared_ptr<celerix::block> const & block_a, celerix::account const & representative_a, celerix::uint128_t const & amount_a)
{
	std::promise<bool> result;
	std::future<bool> future = result.get_future ();
	receive_async (
	block_a->hash (), representative_a, amount_a, block_a->destination (), [&result] (std::shared_ptr<celerix::block> const & block_a) {
		result.set_value (block_a == nullptr);
	},
	true);
	return future.get ();
}

void celerix::wallet::receive_async (celerix::block_hash const & hash_a, celerix::account const & representative_a, celerix::uint128_t const & amount_a, celerix::account const & account_a, std::function<void (std::shared_ptr<celerix::block> const &)> const & action_a, uint64_t work_a, bool generate_work_a)
{
	auto this_l (shared_from_this ());
	wallets.node.wallets.queue_wallet_action (amount_a, this_l, [this_l, hash_a, representative_a, amount_a, account_a, action_a, work_a, generate_work_a] (celerix::wallet & wallet_a) {
		auto block (wallet_a.receive_action (hash_a, representative_a, amount_a, account_a, work_a, generate_work_a));
		action_a (block);
	});
}

celerix::block_hash celerix::wallet::send_sync (celerix::account const & source_a, celerix::account const & account_a, celerix::uint128_t const & amount_a)
{
	std::promise<celerix::block_hash> result;
	std::future<celerix::block_hash> future = result.get_future ();
	send_async (
	source_a, account_a, amount_a, [&result] (std::shared_ptr<celerix::block> const & block_a) {
		result.set_value (block_a->hash ());
	},
	true);
	return future.get ();
}

void celerix::wallet::send_async (celerix::account const & source_a, celerix::account const & account_a, celerix::uint128_t const & amount_a, std::function<void (std::shared_ptr<celerix::block> const &)> const & action_a, uint64_t work_a, bool generate_work_a, boost::optional<std::string> id_a)
{
	auto this_l (shared_from_this ());
	wallets.node.wallets.queue_wallet_action (celerix::wallets::high_priority, this_l, [this_l, source_a, account_a, amount_a, action_a, work_a, generate_work_a, id_a] (celerix::wallet & wallet_a) {
		auto block (wallet_a.send_action (source_a, account_a, amount_a, work_a, generate_work_a, id_a));
		action_a (block);
	});
}

// Update work for account if latest root is root_a
void celerix::wallet::work_update (store::transaction const & transaction_a, celerix::account const & account_a, celerix::root const & root_a, uint64_t work_a)
{
	debug_assert (!wallets.node.network_params.work.validate_entry (celerix::work_version::work_1, root_a, work_a));
	debug_assert (store.exists (transaction_a, account_a));
	auto block_transaction = wallets.node.ledger.tx_begin_read ();
	auto latest (wallets.node.ledger.latest_root (block_transaction, account_a));
	if (latest == root_a)
	{
		store.work_put (transaction_a, account_a, work_a);
	}
	else
	{
		logger.warn (celerix::log::type::wallet, "Cached work no longer valid, discarding");
	}
}

void celerix::wallet::work_ensure (celerix::account const & account_a, celerix::root const & root_a)
{
	using namespace std::chrono_literals;
	std::chrono::seconds const precache_delay = wallets.node.network_params.network.is_dev_network () ? 1s : 10s;

	wallets.delayed_work->operator[] (account_a) = root_a;

	wallets.node.workers.post_delayed (precache_delay, [this_l = shared_from_this (), account_a, root_a] {
		auto delayed_work = this_l->wallets.delayed_work.lock ();
		auto existing (delayed_work->find (account_a));
		if (existing != delayed_work->end () && existing->second == root_a)
		{
			delayed_work->erase (existing);
			this_l->wallets.queue_wallet_action (celerix::wallets::generate_priority, this_l, [account_a, root_a] (celerix::wallet & wallet_a) {
				wallet_a.work_cache_blocking (account_a, root_a);
			});
		}
	});
}

bool celerix::wallet::search_receivable (store::transaction const & wallet_transaction_a)
{
	auto result (!store.valid_password (wallet_transaction_a));
	if (!result)
	{
		logger.info (celerix::log::type::wallet, "Beginning receivable block search");

		for (auto i (store.begin (wallet_transaction_a)), n (store.end (wallet_transaction_a)); i != n; ++i)
		{
			auto block_transaction = wallets.node.ledger.tx_begin_read ();
			celerix::account const & account (i->first);
			// Don't search pending for watch-only accounts
			if (!celerix::wallet_value (i->second).key.is_zero ())
			{
				for (auto j (wallets.node.store.pending.begin (block_transaction, celerix::pending_key (account, 0))), k (wallets.node.store.pending.end (block_transaction)); j != k && celerix::pending_key (j->first).account == account; ++j)
				{
					celerix::pending_key key (j->first);
					auto hash (key.hash);
					celerix::pending_info pending (j->second);
					auto amount (pending.amount.number ());
					if (wallets.node.config.receive_minimum.number () <= amount)
					{
						logger.info (celerix::log::type::wallet, "Found a receivable block {} for account {}", hash.to_string (), pending.source.to_account ());

						if (wallets.node.ledger.confirmed.block_exists_or_pruned (block_transaction, hash))
						{
							auto representative = store.representative (wallet_transaction_a);
							// Receive confirmed block
							receive_async (hash, representative, amount, account, [] (std::shared_ptr<celerix::block> const &) {});
						}
						else if (!wallets.node.confirming_set.contains (hash))
						{
							auto block = wallets.node.ledger.any.block_get (block_transaction, hash);
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

		logger.info (celerix::log::type::wallet, "Receivable block search phase complete");
	}
	else
	{
		logger.warn (celerix::log::type::wallet, "Unable to search receivable blocks, wallet is locked");
	}
	return result;
}

void celerix::wallet::init_free_accounts (store::transaction const & transaction_a)
{
	free_accounts.clear ();
	for (auto i (store.begin (transaction_a)), n (store.end (transaction_a)); i != n; ++i)
	{
		free_accounts.insert (i->first);
	}
}

uint32_t celerix::wallet::deterministic_check (store::transaction const & transaction_a, uint32_t index)
{
	auto block_transaction = wallets.node.ledger.tx_begin_read ();
	for (uint32_t i (index + 1), n (index + 64); i < n; ++i)
	{
		auto prv = store.deterministic_key (transaction_a, i);
		celerix::keypair pair (prv.to_string ());
		// Check if account received at least 1 block
		auto latest (wallets.node.ledger.any.account_head (block_transaction, pair.pub));
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
			auto current = wallets.node.ledger.any.receivable_upper_bound (block_transaction, pair.pub, 0);
			if (current != wallets.node.ledger.any.receivable_end ())
			{
				index = i;
				n = i + 64 + (i / 64);
			}
		}
	}
	return index;
}

celerix::public_key celerix::wallet::change_seed (store::transaction const & transaction_a, celerix::raw_key const & prv_a, uint32_t count)
{
	logger.info (celerix::log::type::wallet, "Changing wallet seed");

	store.seed_set (transaction_a, prv_a);
	auto account = deterministic_insert (transaction_a);
	if (count == 0)
	{
		count = deterministic_check (transaction_a, 0);
		logger.info (celerix::log::type::wallet, "Auto-detected {} accounts to generate", count);
	}
	for (uint32_t i (0); i < count; ++i)
	{
		// Disable work generation to prevent weak CPU nodes stuck
		account = deterministic_insert (transaction_a, false);
	}

	logger.info (celerix::log::type::wallet, "Completed changing wallet seed and generating accounts");

	return account;
}

void celerix::wallet::deterministic_restore (store::transaction const & transaction_a)
{
	auto index (store.deterministic_index_get (transaction_a));
	auto new_index (deterministic_check (transaction_a, index));
	for (uint32_t i (index); i <= new_index && index != new_index; ++i)
	{
		// Disable work generation to prevent weak CPU nodes stuck
		deterministic_insert (transaction_a, false);
	}
}

bool celerix::wallet::live ()
{
	return store.handle != 0;
}

void celerix::wallet::work_cache_blocking (celerix::account const & account_a, celerix::root const & root_a)
{
	if (wallets.node.work_generation_enabled ())
	{
		auto difficulty (wallets.node.default_difficulty (celerix::work_version::work_1));
		auto opt_work_l (wallets.node.work_generate_blocking (celerix::work_version::work_1, root_a, difficulty, account_a));
		if (opt_work_l.has_value ())
		{
			auto transaction_l (wallets.tx_begin_write ());
			if (live () && store.exists (transaction_l, account_a))
			{
				work_update (transaction_l, account_a, root_a, opt_work_l.value ());
			}
		}
		else if (!wallets.node.stopped)
		{
			logger.warn (celerix::log::type::wallet, "Could not precache work for root {} due to work generation failure", root_a.to_string ());
		}
	}
}

/*
 * wallets
 */

void celerix::wallets::do_wallet_actions ()
{
	celerix::unique_lock<celerix::mutex> action_lock{ action_mutex };
	while (!stopped)
	{
		if (!actions.empty ())
		{
			auto first (actions.begin ());
			auto wallet (first->second.first);
			auto current (std::move (first->second.second));
			actions.erase (first);
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

celerix::wallets::wallets (bool error_a, celerix::node & node_a) :
	network_params{ node_a.config.network_params },
	observer ([] (bool) {}),
	kdf{ node_a.config.network_params.kdf_work },
	node (node_a),
	logger (node_a.logger),
	env (boost::polymorphic_downcast<celerix::mdb_wallets_store *> (node_a.wallets_store_impl.get ())->environment),
	stopped (false)
{
	celerix::unique_lock<celerix::mutex> lock{ mutex };
	if (!error_a)
	{
		auto transaction (tx_begin_write ());
		auto status (mdb_dbi_open (env.tx (transaction), nullptr, MDB_CREATE, &handle));
		status |= mdb_dbi_open (env.tx (transaction), "send_action_ids", MDB_CREATE, &send_action_ids);
		release_assert (status == 0);
		std::string beginning (celerix::uint256_union (0).to_string ());
		celerix::store::lmdb::db_val beginning_val{ beginning.size (), const_cast<char *> (beginning.c_str ()) };
		std::string end ((celerix::uint256_union (celerix::uint256_t (0) - celerix::uint256_t (1))).to_string ());
		celerix::store::lmdb::db_val end_val{ end.size (), const_cast<char *> (end.c_str ()) };
		store::iterator i{ store::lmdb::iterator::lower_bound (env.tx (transaction), handle, beginning_val) };
		store::iterator n{ store::lmdb::iterator::lower_bound (env.tx (transaction), handle, end_val) };
		for (; i != n; ++i)
		{
			celerix::wallet_id id;
			std::string text (reinterpret_cast<char const *> (i->first.data ()), i->first.size ());
			auto error (id.decode_hex (text));
			release_assert (!error);
			release_assert (items.find (id) == items.end ());
			auto wallet (std::make_shared<celerix::wallet> (error, transaction, *this, text));
			if (!error)
			{
				items[id] = wallet;
			}
			else
			{
				// Couldn't open wallet
			}
		}
	}
	// Backup before upgrade wallets
	bool backup_required (false);
	if (node.config.backup_before_upgrade)
	{
		auto transaction (tx_begin_read ());
		for (auto & item : items)
		{
			if (item.second->store.version (transaction) != celerix::wallet_store::version_current)
			{
				backup_required = true;
				break;
			}
		}
	}
	if (backup_required)
	{
		char const * store_path;
		mdb_env_get_path (env, &store_path);
		std::filesystem::path const path (store_path);
		celerix::store::lmdb::component::create_backup_file (env, path, node_a.logger);
	}
	for (auto & item : items)
	{
		item.second->enter_initial_password ();
	}
}

celerix::wallets::~wallets ()
{
	stop ();
}

void celerix::wallets::start ()
{
	thread = std::thread{ [this] () {
		celerix::thread_role::set (celerix::thread_role::name::wallet_actions);
		do_wallet_actions ();
	} };

	if (node.config.enable_voting)
	{
		ongoing_compute_reps ();
	}
}

void celerix::wallets::stop ()
{
	{
		celerix::lock_guard<celerix::mutex> action_lock{ action_mutex };
		stopped = true;
		actions.clear ();
	}
	condition.notify_all ();
	if (thread.joinable ())
	{
		thread.join ();
	}
}

std::shared_ptr<celerix::wallet> celerix::wallets::open (celerix::wallet_id const & id_a)
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	std::shared_ptr<celerix::wallet> result;
	auto existing (items.find (id_a));
	if (existing != items.end ())
	{
		result = existing->second;
	}
	return result;
}

std::shared_ptr<celerix::wallet> celerix::wallets::create (celerix::wallet_id const & id_a)
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	debug_assert (items.find (id_a) == items.end ());
	std::shared_ptr<celerix::wallet> result;
	bool error;
	{
		auto transaction (tx_begin_write ());
		result = std::make_shared<celerix::wallet> (error, transaction, *this, id_a.to_string ());
	}
	if (!error)
	{
		items[id_a] = result;
		result->enter_initial_password ();
	}
	return result;
}

bool celerix::wallets::search_receivable (celerix::wallet_id const & wallet_a)
{
	auto result (false);
	if (auto wallet = open (wallet_a); wallet != nullptr)
	{
		result = wallet->search_receivable (tx_begin_read ());
	}
	return result;
}

void celerix::wallets::search_receivable_all ()
{
	celerix::unique_lock<celerix::mutex> lk{ mutex };
	auto wallets_l = get_wallets ();
	auto wallet_transaction (tx_begin_read ());
	lk.unlock ();
	for (auto const & [id, wallet] : wallets_l)
	{
		wallet->search_receivable (wallet_transaction);
	}
}

void celerix::wallets::destroy (celerix::wallet_id const & id_a)
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	auto transaction (tx_begin_write ());
	// action_mutex should be after transactions to prevent deadlocks in deterministic_insert () & insert_adhoc ()
	celerix::lock_guard<celerix::mutex> action_lock{ action_mutex };
	auto existing (items.find (id_a));
	debug_assert (existing != items.end ());
	auto wallet (existing->second);
	items.erase (existing);
	wallet->store.destroy (transaction);
}

void celerix::wallets::reload ()
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	auto transaction (tx_begin_write ());
	std::unordered_set<celerix::uint256_union> stored_items;
	std::string beginning (celerix::uint256_union (0).to_string ());
	celerix::store::lmdb::db_val beginning_val{ beginning.size (), const_cast<char *> (beginning.c_str ()) };
	std::string end ((celerix::uint256_union (celerix::uint256_t (0) - celerix::uint256_t (1))).to_string ());
	celerix::store::lmdb::db_val end_val{ end.size (), const_cast<char *> (end.c_str ()) };
	store::iterator i{ store::lmdb::iterator::lower_bound (env.tx (transaction), handle, beginning_val) };
	store::iterator n{ store::lmdb::iterator::lower_bound (env.tx (transaction), handle, end_val) };
	for (; i != n; ++i)
	{
		celerix::wallet_id id;
		std::string text (reinterpret_cast<char const *> (i->first.data ()), i->first.size ());
		auto error (id.decode_hex (text));
		debug_assert (!error);
		// New wallet
		if (items.find (id) == items.end ())
		{
			auto wallet (std::make_shared<celerix::wallet> (error, transaction, *this, text));
			if (!error)
			{
				items[id] = wallet;
			}
		}
		// List of wallets on disk
		stored_items.insert (id);
	}
	// Delete non existing wallets from memory
	std::vector<celerix::wallet_id> deleted_items;
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

void celerix::wallets::queue_wallet_action (celerix::uint128_t const & amount_a, std::shared_ptr<celerix::wallet> const & wallet_a, std::function<void (celerix::wallet &)> action_a)
{
	{
		celerix::lock_guard<celerix::mutex> action_lock{ action_mutex };
		actions.emplace (amount_a, std::make_pair (wallet_a, action_a));
	}
	condition.notify_all ();
}

void celerix::wallets::foreach_representative (std::function<void (celerix::public_key const & pub_a, celerix::raw_key const & prv_a)> const & action_a)
{
	if (node.config.enable_voting)
	{
		std::vector<std::pair<celerix::public_key const, celerix::raw_key const>> action_accounts_l;
		{
			auto transaction_l (tx_begin_read ());
			auto ledger_txn = node.ledger.tx_begin_read ();
			celerix::lock_guard<celerix::mutex> lock{ mutex };
			for (auto i (items.begin ()), n (items.end ()); i != n; ++i)
			{
				auto & wallet (*i->second);
				celerix::lock_guard<std::recursive_mutex> store_lock{ wallet.store.mutex };
				decltype (wallet.representatives) representatives_l;
				{
					celerix::lock_guard<celerix::mutex> representatives_lock{ wallet.representatives_mutex };
					representatives_l = wallet.representatives;
				}
				for (auto const & account : representatives_l)
				{
					if (wallet.store.exists (transaction_l, account))
					{
						if (!node.ledger.weight_exact (ledger_txn, account).is_zero ())
						{
							if (wallet.store.valid_password (transaction_l))
							{
								celerix::raw_key prv;
								auto error (wallet.store.fetch (transaction_l, account, prv));
								(void)error;
								debug_assert (!error);
								action_accounts_l.emplace_back (account, prv);
							}
							else
							{
								// TODO: Better logging interval handling
								static auto last_log = std::chrono::steady_clock::time_point ();
								if (last_log < std::chrono::steady_clock::now () - std::chrono::seconds (60))
								{
									last_log = std::chrono::steady_clock::now ();

									logger.warn (celerix::log::type::wallet, "Representative locked inside wallet: {}", i->first.to_string ());
								}
							}
						}
					}
				}
			}
		}
		for (auto const & representative : action_accounts_l)
		{
			action_a (representative.first, representative.second);
		}
	}
}

bool celerix::wallets::exists (store::transaction const & transaction_a, celerix::account const & account_a)
{
	celerix::lock_guard<celerix::mutex> lock{ mutex };
	auto result (false);
	for (auto i (items.begin ()), n (items.end ()); !result && i != n; ++i)
	{
		result = i->second->store.exists (transaction_a, account_a);
	}
	return result;
}

celerix::store::write_transaction celerix::wallets::tx_begin_write ()
{
	return env.tx_begin_write ();
}

celerix::store::read_transaction celerix::wallets::tx_begin_read ()
{
	return env.tx_begin_read ();
}

void celerix::wallets::clear_send_ids (store::transaction const & transaction_a)
{
	auto status (mdb_drop (env.tx (transaction_a), send_action_ids, 0));
	(void)status;
	debug_assert (status == 0);
}

celerix::wallet_representatives celerix::wallets::reps () const
{
	celerix::lock_guard<celerix::mutex> counts_guard{ reps_cache_mutex };
	return representatives;
}

bool celerix::wallets::check_rep (celerix::account const & account_a, celerix::uint128_t const & half_principal_weight_a, bool const acquire_lock_a)
{
	auto weight = node.ledger.weight (account_a);
	if (weight < node.config.vote_minimum.number ())
	{
		return false; // account not a representative
	}

	celerix::unique_lock<celerix::mutex> lock;
	if (acquire_lock_a)
	{
		lock = celerix::unique_lock<celerix::mutex>{ reps_cache_mutex };
	}

	if (weight >= half_principal_weight_a)
	{
		representatives.half_principal = true;
	}

	auto insert_result = representatives.accounts.insert (account_a);
	if (!insert_result.second)
	{
		return false; // account already exists
	}

	++representatives.voting;

	return true;
}

void celerix::wallets::compute_reps ()
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };
	celerix::lock_guard<celerix::mutex> counts_guard{ reps_cache_mutex };
	representatives.clear ();
	auto half_principal_weight (node.minimum_principal_weight () / 2);
	auto transaction (tx_begin_read ());
	for (auto i (items.begin ()), n (items.end ()); i != n; ++i)
	{
		auto & wallet (*i->second);
		decltype (wallet.representatives) representatives_l;
		for (auto ii (wallet.store.begin (transaction)), nn (wallet.store.end (transaction)); ii != nn; ++ii)
		{
			auto account (ii->first);
			if (check_rep (account, half_principal_weight, false))
			{
				representatives_l.insert (account);
			}
		}
		celerix::lock_guard<celerix::mutex> representatives_guard{ wallet.representatives_mutex };
		wallet.representatives.swap (representatives_l);
	}
}

void celerix::wallets::ongoing_compute_reps ()
{
	compute_reps ();
	auto & node_l (node);
	// Representation drifts quickly on the test network but very slowly on the live network
	auto compute_delay = network_params.network.is_dev_network () ? std::chrono::milliseconds (10) : (network_params.network.is_test_network () ? std::chrono::milliseconds (celerix::test_scan_wallet_reps_delay ()) : std::chrono::minutes (15));
	node.workers.post_delayed (compute_delay, [&node_l] () {
		node_l.wallets.ongoing_compute_reps ();
	});
}

void celerix::wallets::receive_confirmed (celerix::block_hash const & hash_a, celerix::account const & destination_a)
{
	celerix::unique_lock<celerix::mutex> lk{ mutex };
	auto wallets_l = get_wallets ();
	auto wallet_transaction = tx_begin_read ();
	lk.unlock ();
	for ([[maybe_unused]] auto const & [id, wallet] : wallets_l)
	{
		if (wallet->store.exists (wallet_transaction, destination_a))
		{
			celerix::account representative;
			representative = wallet->store.representative (wallet_transaction);
			auto pending = node.ledger.any.pending_get (node.ledger.tx_begin_read (), celerix::pending_key (destination_a, hash_a));
			if (pending)
			{
				auto amount (pending->amount.number ());
				wallet->receive_async (hash_a, representative, amount, destination_a, [] (std::shared_ptr<celerix::block> const &) {});
			}
			else
			{
				if (!node.ledger.confirmed.block_exists_or_pruned (node.ledger.tx_begin_read (), hash_a))
				{
					logger.warn (celerix::log::type::wallet, "Confirmed block is missing: {}", hash_a.to_string ());
					debug_assert (false, "confirmed block is missing");
				}
				else
				{
					logger.warn (celerix::log::type::wallet, "Block has already been received: {}", hash_a.to_string ());
				}
			}
		}
	}
}

std::unordered_map<celerix::wallet_id, std::shared_ptr<celerix::wallet>> celerix::wallets::get_wallets ()
{
	debug_assert (!mutex.try_lock ());
	return items;
}

celerix::uint128_t const celerix::wallets::generate_priority = std::numeric_limits<celerix::uint128_t>::max ();
celerix::uint128_t const celerix::wallets::high_priority = std::numeric_limits<celerix::uint128_t>::max () - 1;

auto celerix::wallet_store::begin (store::transaction const & transaction_a) -> iterator
{
	celerix::account account{ special_count };
	celerix::store::lmdb::db_val val{ account };
	return iterator{ store::iterator{ store::lmdb::iterator::lower_bound (env.tx (transaction_a), handle, val) } };
}

auto celerix::wallet_store::begin (store::transaction const & transaction_a, celerix::account const & key) -> iterator
{
	celerix::account account (key);
	celerix::store::lmdb::db_val val{ account };
	return iterator{ store::iterator{ store::lmdb::iterator::lower_bound (env.tx (transaction_a), handle, val) } };
}

auto celerix::wallet_store::find (store::transaction const & transaction_a, celerix::account const & key) -> iterator
{
	auto result = begin (transaction_a, key);
	auto end = this->end (transaction_a);
	if (result != end)
	{
		if (result->first == key)
		{
			return result;
		}
		else
		{
			return end;
		}
	}
	else
	{
		return end;
	}
	return result;
}

auto celerix::wallet_store::end (store::transaction const & transaction_a) -> iterator
{
	return iterator{ store::iterator{ store::lmdb::iterator::end (env.tx (transaction_a), handle) } };
}

celerix::mdb_wallets_store::mdb_wallets_store (std::filesystem::path const & path_a, celerix::lmdb_config const & lmdb_config_a) :
	environment (error, path_a, celerix::store::lmdb::env::options::make ().set_config (lmdb_config_a).override_config_sync (celerix::lmdb_config::sync_strategy::always).override_config_map_size (1ULL * 1024 * 1024 * 1024))
{
}

bool celerix::mdb_wallets_store::init_error () const
{
	return error;
}

celerix::container_info celerix::wallets::container_info () const
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };

	celerix::container_info info;
	info.put ("items", items.size ());
	info.put ("actions", actions.size ());
	return info;
}
