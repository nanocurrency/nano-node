#include <rai/node/wallet.hpp>

#include <rai/node/node.hpp>

#include <argon2.h>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <ed25519-donna/ed25519.h>

#include <future>

rai::work_pool::work_pool () :
current (0),
ticket (0),
done (false)
{
	static_assert (ATOMIC_INT_LOCK_FREE == 2, "Atomic int needed");
	auto count (std::max (1u, std::thread::hardware_concurrency ()));
	for (auto i (0); i < count; ++i)
	{
		auto thread (std::thread ([this, i] ()
		{
			rai::work_thread_reprioritize ();
			loop (i);
		}));
		threads.push_back (std::move (thread));
	}
}

rai::work_pool::~work_pool ()
{
	stop ();
	for (auto &i: threads)
	{
		i.join ();
	}
}

namespace
{
class xorshift1024star
{
public:
    xorshift1024star ():
    p (0)
    {
    }
    std::array <uint64_t, 16> s;
    unsigned p;
    uint64_t next ()
    {
        auto p_l (p);
        auto pn ((p_l + 1) & 15);
        p = pn;
        uint64_t s0 = s[ p_l ];
        uint64_t s1 = s[ pn ];
        s1 ^= s1 << 31; // a
        s1 ^= s1 >> 11; // b
        s0 ^= s0 >> 30; // c
        return ( s[ pn ] = s0 ^ s1 ) * 1181783497276652981LL;
    }
};
}

uint64_t rai::work_pool::work_value (rai::block_hash const & root_a, uint64_t work_a)
{
	uint64_t result;
    blake2b_state hash;
	blake2b_init (&hash, sizeof (result));
    blake2b_update (&hash, reinterpret_cast <uint8_t *> (&work_a), sizeof (work_a));
    blake2b_update (&hash, root_a.bytes.data (), root_a.bytes.size ());
    blake2b_final (&hash, reinterpret_cast <uint8_t *> (&result), sizeof (result));
	return result;
}

void rai::work_pool::loop (uint64_t thread)
{
    xorshift1024star rng;
	rai::random_pool.GenerateBlock (reinterpret_cast <uint8_t *> (rng.s.data ()),  rng.s.size () * sizeof (decltype (rng.s)::value_type));
	uint64_t work;
	uint64_t output;
    blake2b_state hash;
	blake2b_init (&hash, sizeof (output));
	std::unique_lock <std::mutex> lock (mutex);
	while (!done || !pending.empty())
	{
		auto current_l (current);
		if (!current_l.is_zero ())
		{
			int ticket_l (ticket);
			lock.unlock ();
			output = 0;
			while (ticket == ticket_l && output < rai::work_pool::publish_threshold)
			{
				unsigned iteration (256);
				while (iteration && output < rai::work_pool::publish_threshold)
				{
					work = rng.next ();
					blake2b_update (&hash, reinterpret_cast <uint8_t *> (&work), sizeof (work));
					blake2b_update (&hash, current_l.bytes.data (), current_l.bytes.size ());
					blake2b_final (&hash, reinterpret_cast <uint8_t *> (&output), sizeof (output));
					blake2b_init (&hash, sizeof (output));
					iteration -= 1;
				}
			}
			lock.lock ();
			if (current == current_l && output >= rai::work_pool::publish_threshold)
			{
				assert (output >= rai::work_pool::publish_threshold);
				assert (work_value (current_l, work) == output);
				++ticket;
				completed [current_l] = work;
				consumer_condition.notify_all ();
				// Change current so only one work thread publishes their result
				current.clear ();
			}
			else
			{
			}
		}
		else
		{
			if (!pending.empty ())
			{
				current = pending.front ();
				pending.pop_front ();
				producer_condition.notify_all ();
			}
			else
			{
				producer_condition.wait (lock);
			}
		}
	}
}

void rai::work_pool::cancel (rai::uint256_union const & root_a)
{
	std::lock_guard <std::mutex> lock (mutex);
	if (current == root_a)
	{
		++ticket;
		completed [root_a] = boost::none;
		current.clear ();
	}
	else
	{
		auto existing (std::find (pending.begin (), pending.end (), root_a));
		if (existing != pending.end ())
		{
			pending.erase (existing);
			completed [root_a] = boost::none;
		}
		else
		{
			// Requested something that we're no longer working on
		}
	}
	consumer_condition.notify_all ();
}

bool rai::work_pool::work_validate (rai::block_hash const & root_a, uint64_t work_a)
{
    auto result (work_value (root_a, work_a) < rai::work_pool::publish_threshold);
	return result;
}

bool rai::work_pool::work_validate (rai::block & block_a)
{
    return work_validate (block_a.root (), block_a.block_work ());
}

void rai::work_pool::stop ()
{
	std::lock_guard <std::mutex> lock (mutex);
	done = true;
	producer_condition.notify_all ();
}

boost::optional <uint64_t> rai::work_pool::generate_maybe (rai::uint256_union const & root_a)
{
	assert (!root_a.is_zero ());
	boost::optional <uint64_t> result;
	std::unique_lock <std::mutex> lock (mutex);
	pending.push_back (root_a);
	producer_condition.notify_all ();
	auto done (false);
	while (!done)
	{
		consumer_condition.wait (lock);
		auto finish (completed.find (root_a));
		if (finish != completed.end ())
		{
			done = true;
			result = finish->second;
			completed.erase (finish);
		}
	}
	return result;
}

uint64_t rai::work_pool::generate (rai::uint256_union const & root_a)
{
	return generate_maybe (root_a).value ();
}

rai::uint256_union rai::wallet_store::check (MDB_txn * transaction_a)
{
	rai::wallet_value value (entry_get_raw (transaction_a, rai::wallet_store::check_special));
    return value.key;
}

rai::uint256_union rai::wallet_store::salt (MDB_txn * transaction_a)
{
	rai::wallet_value value (entry_get_raw (transaction_a, rai::wallet_store::salt_special));
    return value.key;
}

void rai::wallet_store::wallet_key (rai::raw_key & prv_a, MDB_txn * transaction_a)
{
	rai::wallet_value value (entry_get_raw (transaction_a, rai::wallet_store::wallet_key_special));
    rai::raw_key password_l;
	password.value (password_l);
    prv_a.decrypt (value.key, password_l, salt (transaction_a).owords [0]);
}

void rai::wallet_store::seed (rai::raw_key & prv_a, MDB_txn * transaction_a)
{
	rai::wallet_value value (entry_get_raw (transaction_a, rai::wallet_store::seed_special));
	rai::raw_key password_l;
	wallet_key (password_l, transaction_a);
	prv_a.decrypt (value.key, password_l, salt (transaction_a).owords [0]);
}

void rai::wallet_store::seed_set (MDB_txn * transaction_a, rai::raw_key const & prv_a)
{
	rai::raw_key password_l;
	wallet_key (password_l, transaction_a);
	rai::uint256_union ciphertext;
	ciphertext.encrypt (prv_a, password_l, salt (transaction_a).owords [0]);
	entry_put_raw (transaction_a, rai::wallet_store::seed_special, rai::wallet_value (ciphertext));
	deterministic_clear (transaction_a);
}

rai::public_key rai::wallet_store::deterministic_insert (MDB_txn * transaction_a)
{
	auto index (deterministic_index_get (transaction_a));
	rai::raw_key prv;
	deterministic_key (prv, transaction_a, index);
    rai::public_key result;
    ed25519_publickey (prv.data.bytes.data (), result.bytes.data ());
	while (exists (transaction_a, result))
	{
		++index;
		deterministic_key (prv, transaction_a, index);
		ed25519_publickey (prv.data.bytes.data (), result.bytes.data ());
	}
	uint64_t marker (1);
	marker <<= 32;
	marker |= index;
	entry_put_raw (transaction_a, result, rai::uint256_union (marker));
	++index;
	deterministic_index_set (transaction_a, index);
	return result;
}

void rai::wallet_store::deterministic_key (rai::raw_key & prv_a, MDB_txn * transaction_a, uint32_t index_a)
{
	assert (valid_password (transaction_a));
	rai::raw_key seed_l;
	seed (seed_l, transaction_a);
    blake2b_state hash;
	blake2b_init (&hash, prv_a.data.bytes.size ());
    blake2b_update (&hash, seed_l.data.bytes.data (), seed_l.data.bytes.size ());
	rai::uint256_union index (index_a);
    blake2b_update (&hash, reinterpret_cast <uint8_t *> (&index.dwords [7]), sizeof (uint32_t));
    blake2b_final (&hash, prv_a.data.bytes.data (), prv_a.data.bytes.size ());
}

uint32_t rai::wallet_store::deterministic_index_get (MDB_txn * transaction_a)
{
	rai::wallet_value value (entry_get_raw (transaction_a, rai::wallet_store::deterministic_index_special));
	return value.key.number ().convert_to <uint32_t> ();
}

void rai::wallet_store::deterministic_index_set (MDB_txn * transaction_a, uint32_t index_a)
{
	rai::uint256_union index_l (index_a);
	rai::wallet_value value (index_l);
	entry_put_raw (transaction_a, rai::wallet_store::deterministic_index_special, value);
}

void rai::wallet_store::deterministic_clear (MDB_txn * transaction_a)
{
	rai::uint256_union key (0);
	for (auto i (begin (transaction_a)), n (end ()); i != n;)
	{
		switch (key_type (rai::wallet_value (i->second)))
		{
		case rai::key_type::deterministic:
		{
			rai::uint256_union key (i->first);
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

bool rai::wallet_store::valid_password (MDB_txn * transaction_a)
{
    rai::raw_key zero;
    zero.data.clear ();
    rai::raw_key wallet_key_l;
	wallet_key (wallet_key_l, transaction_a);
    rai::uint256_union check_l;
	check_l.encrypt (zero, wallet_key_l, salt (transaction_a).owords [0]);
    return check (transaction_a) == check_l;
}

bool rai::wallet_store::attempt_password (MDB_txn * transaction_a, std::string const & password_a)
{
	rai::raw_key password_l;
	derive_key (password_l, transaction_a, password_a);
    password.value_set (password_l);
	auto result (!valid_password (transaction_a));
	if (!result)
	{
		if (version (transaction_a) == version_1)
		{
			upgrade_v1_v2 ();
		}
		if (version (transaction_a) == version_2)
		{
			upgrade_v2_v3 ();
		}
	}
	return result;
}

bool rai::wallet_store::rekey (MDB_txn * transaction_a, std::string const & password_a)
{
    bool result (false);
	if (valid_password (transaction_a))
    {
        rai::raw_key password_new;
		derive_key (password_new, transaction_a, password_a);
        rai::raw_key wallet_key_l;
		wallet_key (wallet_key_l, transaction_a);
        rai::raw_key password_l;
		password.value (password_l);
        (*password.values [0]) ^= password_l.data;
        (*password.values [0]) ^= password_new.data;
        rai::uint256_union encrypted;
		encrypted.encrypt (wallet_key_l, password_new, salt (transaction_a).owords [0]);
		entry_put_raw (transaction_a, rai::wallet_store::wallet_key_special, rai::wallet_value (encrypted));
    }
    else
    {
        result = true;
    }
    return result;
}

void rai::wallet_store::derive_key (rai::raw_key & prv_a, MDB_txn * transaction_a, std::string const & password_a)
{
	auto salt_l (salt (transaction_a));
	kdf.phs (prv_a, password_a, salt_l);
}

rai::wallet_value::wallet_value (MDB_val const & val_a)
{
	assert (val_a.mv_size == sizeof (*this));
	std::copy (reinterpret_cast <uint8_t const *> (val_a.mv_data), reinterpret_cast <uint8_t const *> (val_a.mv_data) + sizeof (key), key.chars.begin ());
	std::copy (reinterpret_cast <uint8_t const *> (val_a.mv_data) + sizeof (key), reinterpret_cast <uint8_t const *> (val_a.mv_data) + sizeof (key) + sizeof (work), reinterpret_cast <char *> (&work));
}

rai::wallet_value::wallet_value (rai::uint256_union const & value_a) :
key (value_a),
work (0)
{
}

rai::mdb_val rai::wallet_value::val () const
{
	static_assert (sizeof (*this) == sizeof (key) + sizeof (work), "Class not packed");
	return rai::mdb_val (sizeof (*this), const_cast <rai::wallet_value *> (this));
}

unsigned const rai::wallet_store::version_1 (1);
unsigned const rai::wallet_store::version_2 (2);
unsigned const rai::wallet_store::version_3 (3);
unsigned const rai::wallet_store::version_current (version_3);
rai::uint256_union const rai::wallet_store::version_special (0);
rai::uint256_union const rai::wallet_store::salt_special (1);
rai::uint256_union const rai::wallet_store::wallet_key_special (2);
rai::uint256_union const rai::wallet_store::check_special (3);
rai::uint256_union const rai::wallet_store::representative_special (4);
rai::uint256_union const rai::wallet_store::seed_special (5);
rai::uint256_union const rai::wallet_store::deterministic_index_special (6);
int const rai::wallet_store::special_count (7);

rai::wallet_store::wallet_store (bool & init_a, rai::kdf & kdf_a, rai::transaction & transaction_a, rai::account representative_a, unsigned fanout_a, std::string const & wallet_a, std::string const & json_a) :
password (0, fanout_a),
kdf (kdf_a),
environment (transaction_a.environment)
{
    init_a = false;
    initialize (transaction_a, init_a, wallet_a);
    if (!init_a)
    {
        MDB_val junk;
		assert (mdb_get (transaction_a, handle, version_special.val (), &junk) == MDB_NOTFOUND);
        boost::property_tree::ptree wallet_l;
        std::stringstream istream (json_a);
        boost::property_tree::read_json (istream, wallet_l);
        for (auto i (wallet_l.begin ()), n (wallet_l.end ()); i != n; ++i)
        {
            rai::uint256_union key;
            init_a = key.decode_hex (i->first);
            if (!init_a)
            {
                rai::uint256_union value;
                init_a = value.decode_hex (wallet_l.get <std::string> (i->first));
                if (!init_a)
                {
					entry_put_raw (transaction_a, key, rai::wallet_value (value));
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
		init_a |= mdb_get (transaction_a, handle, version_special.val (), &junk) != 0;
		init_a |= mdb_get (transaction_a, handle, wallet_key_special.val (), & junk) != 0;
		init_a |= mdb_get (transaction_a, handle, salt_special.val (), &junk) != 0;
		init_a |= mdb_get (transaction_a, handle, check_special.val (), &junk) != 0;
		init_a |= mdb_get (transaction_a, handle, representative_special.val (), &junk) != 0;
		rai::raw_key key;
		key.data.clear();
		password.value_set (key);
    }
}

rai::wallet_store::wallet_store (bool & init_a, rai::kdf & kdf_a, rai::transaction & transaction_a, rai::account representative_a, unsigned fanout_a, std::string const & wallet_a) :
password (0, fanout_a),
kdf (kdf_a),
environment (transaction_a.environment)
{
    init_a = false;
    initialize (transaction_a, init_a, wallet_a);
    if (!init_a)
    {
		int version_status;
		MDB_val version_value;
		version_status = mdb_get (transaction_a, handle, version_special.val (), &version_value);
        if (version_status == MDB_NOTFOUND)
        {
			version_put (transaction_a, version_current);
            rai::uint256_union salt_l;
            random_pool.GenerateBlock (salt_l.bytes.data (), salt_l.bytes.size ());
			entry_put_raw (transaction_a, rai::wallet_store::salt_special, rai::wallet_value (salt_l));
            // Wallet key is a fixed random key that encrypts all entries
            rai::raw_key wallet_key;
            random_pool.GenerateBlock (wallet_key.data.bytes.data (), sizeof (wallet_key.data.bytes));
			rai::raw_key password_l;
			password_l.data.clear ();
            password.value_set (password_l);
            rai::raw_key zero;
			zero.data.clear ();
            // Wallet key is encrypted by the user's password
            rai::uint256_union encrypted;
			encrypted.encrypt (wallet_key, zero, salt_l.owords [0]);
			entry_put_raw (transaction_a, rai::wallet_store::wallet_key_special, rai::wallet_value (encrypted));
            rai::uint256_union check;
			check.encrypt (zero, wallet_key, salt_l.owords [0]);
			entry_put_raw (transaction_a, rai::wallet_store::check_special, rai::wallet_value (check));
			entry_put_raw (transaction_a, rai::wallet_store::representative_special, rai::wallet_value (representative_a));
			rai::raw_key seed;
			random_pool.GenerateBlock (seed.data.bytes.data (), seed.data.bytes.size ());
			seed_set (transaction_a, seed);
			entry_put_raw (transaction_a, rai::wallet_store::deterministic_index_special, rai::wallet_value (0));
        }
    }
}

std::vector <rai::account> rai::wallet_store::accounts (MDB_txn * transaction_a)
{
	std::vector <rai::account> result;
	for (auto i (begin (transaction_a)), n (end ()); i != n; ++i)
	{
		rai::account account (i->first);
		result.push_back (account);
	}
	return result;
}

void rai::wallet_store::initialize (MDB_txn * transaction_a, bool & init_a, std::string const & path_a)
{
	assert (strlen (path_a.c_str ()) == path_a.size ());
	auto error (mdb_dbi_open (transaction_a, path_a.c_str (), MDB_CREATE, &handle));
	init_a = error != 0;
}

bool rai::wallet_store::is_representative (MDB_txn * transaction_a)
{
    return exists (transaction_a, representative (transaction_a));
}

void rai::wallet_store::representative_set (MDB_txn * transaction_a, rai::account const & representative_a)
{
	entry_put_raw (transaction_a, rai::wallet_store::representative_special, rai::wallet_value (representative_a));
}

rai::account rai::wallet_store::representative (MDB_txn * transaction_a)
{
	rai::wallet_value value (entry_get_raw (transaction_a, rai::wallet_store::representative_special));
    return value.key;
}

rai::public_key rai::wallet_store::insert_adhoc (MDB_txn * transaction_a, rai::raw_key const & prv)
{
	assert (valid_password (transaction_a));
    rai::public_key pub;
    ed25519_publickey (prv.data.bytes.data (), pub.bytes.data ());
	rai::raw_key password_l;
	wallet_key (password_l, transaction_a);
	rai::uint256_union ciphertext;
	ciphertext.encrypt (prv, password_l, salt (transaction_a).owords [0]);
	entry_put_raw (transaction_a, pub, rai::wallet_value (ciphertext));
	return pub;
}

void rai::wallet_store::erase (MDB_txn * transaction_a, rai::public_key const & pub)
{
	auto status (mdb_del (transaction_a, handle, pub.val (), nullptr));
    assert (status == 0);
}

rai::wallet_value rai::wallet_store::entry_get_raw (MDB_txn * transaction_a, rai::public_key const & pub_a)
{
	rai::wallet_value result;
	MDB_val value;
	auto status (mdb_get (transaction_a, handle, pub_a.val (), &value));
	if (status == 0)
	{
		result = rai::wallet_value (value);
	}
	else
	{
		result.key.clear ();
		result.work = 0;
	}
	return result;
}

void rai::wallet_store::entry_put_raw (MDB_txn * transaction_a, rai::public_key const & pub_a, rai::wallet_value const & entry_a)
{
	auto status (mdb_put (transaction_a, handle, pub_a.val (), entry_a.val (), 0));
	assert (status == 0);
}

rai::key_type rai::wallet_store::key_type (rai::wallet_value const & value_a)
{
	auto number (value_a.key.number ());
	rai::key_type result;
	auto text (number.convert_to <std::string> ());
	if (number > std::numeric_limits <uint64_t>::max ())
	{
		result = rai::key_type::adhoc;
	}
	else
	{
		if ((number >> 32).convert_to <uint32_t> () == 1)
		{
			result = rai::key_type::deterministic;
		}
		else
		{
			result = rai::key_type::unknown;
		}
	}
	return result;
}

bool rai::wallet_store::fetch (MDB_txn * transaction_a, rai::public_key const & pub, rai::raw_key & prv)
{
    auto result (false);
	if (valid_password (transaction_a))
	{
		rai::wallet_value value (entry_get_raw (transaction_a, pub));
		if (!value.key.is_zero ())
		{
			switch (key_type (value))
			{
			case rai::key_type::deterministic:
			{
				rai::raw_key seed_l;
				seed (seed_l, transaction_a);
				uint32_t index (value.key.number ().convert_to <uint32_t> ());
				deterministic_key (prv, transaction_a, index);
				break;
			}
			case rai::key_type::adhoc:
			{
				// Ad-hoc keys
				rai::raw_key password_l;
				wallet_key (password_l, transaction_a);
				prv.decrypt (value.key, password_l, salt (transaction_a).owords [0]);
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
		rai::public_key compare;
		ed25519_publickey (prv.data.bytes.data (), compare.bytes.data ());
		if (!(pub == compare))
		{
			result = true;
		}
	}
    return result;
}

bool rai::wallet_store::exists (MDB_txn * transaction_a, rai::public_key const & pub)
{
    return find (transaction_a, pub) != end ();
}

void rai::wallet_store::serialize_json (MDB_txn * transaction_a, std::string & string_a)
{
    boost::property_tree::ptree tree;
    for (rai::store_iterator i (transaction_a, handle), n (nullptr); i != n; ++i)
    {
        tree.put (rai::uint256_union (i->first).to_string (), rai::wallet_value (i->second).key.to_string ());
    }
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, tree);
    string_a = ostream.str ();
}

void rai::wallet_store::write_backup (MDB_txn * transaction_a, boost::filesystem::path const & path_a)
{
	std::ofstream backup_file;
	backup_file.open (path_a.string ());
	if (!backup_file.fail ())
	{
		std::string json;
		serialize_json (transaction_a, json);
		backup_file << json;
	}
}

bool rai::wallet_store::move (MDB_txn * transaction_a, rai::wallet_store & other_a, std::vector <rai::public_key> const & keys)
{
    assert (valid_password (transaction_a));
    assert (other_a.valid_password (transaction_a));
    auto result (false);
    for (auto i (keys.begin ()), n (keys.end ()); i != n; ++i)
    {
        rai::raw_key prv;
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

bool rai::wallet_store::import (MDB_txn * transaction_a, rai::wallet_store & other_a)
{
    assert (valid_password (transaction_a));
    assert (other_a.valid_password (transaction_a));
    auto result (false);
    for (auto i (other_a.begin (transaction_a)), n (end ()); i != n; ++i)
    {
        rai::raw_key prv;
        auto error (other_a.fetch (transaction_a, i->first, prv));
        result = result | error;
        if (!result)
        {
            insert_adhoc (transaction_a, prv);
            other_a.erase (transaction_a, i->first);
        }
    }
    return result;
}

bool rai::wallet_store::work_get (MDB_txn * transaction_a, rai::public_key const & pub_a, uint64_t & work_a)
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

void rai::wallet_store::work_put (MDB_txn * transaction_a, rai::public_key const & pub_a, uint64_t work_a)
{
	auto entry (entry_get_raw (transaction_a, pub_a));
	assert (!entry.key.is_zero ());
	entry.work = work_a;
	entry_put_raw (transaction_a, pub_a, entry);
}

unsigned rai::wallet_store::version (MDB_txn * transaction_a)
{
	rai::wallet_value value (entry_get_raw (transaction_a, rai::wallet_store::version_special));
	auto entry (value.key);
	auto result (static_cast <unsigned> (entry.bytes [31]));
	return result;
}

void rai::wallet_store::version_put (MDB_txn * transaction_a, unsigned version_a)
{
	rai::uint256_union entry (version_a);
	entry_put_raw (transaction_a, rai::wallet_store::version_special, rai::wallet_value (entry));
}

void rai::wallet_store::upgrade_v1_v2 ()
{
	rai::transaction transaction (environment, nullptr, true);
	assert (version (transaction) == 1);
	rai::raw_key zero_password;
	rai::wallet_value value (entry_get_raw (transaction, rai::wallet_store::wallet_key_special));
    rai::raw_key kdf;
	kdf.data.clear ();
    zero_password.decrypt (value.key, kdf, salt (transaction).owords [0]);
	derive_key (kdf, transaction, "");
	rai::raw_key empty_password;
	empty_password.decrypt (value.key, kdf, salt (transaction).owords [0]);
	for (auto i (begin (transaction)), n (end ()); i != n; ++i)
	{
		rai::public_key key (i->first);
		rai::raw_key prv;
		if (fetch (transaction, key, prv))
		{
			// Key failed to decrypt despite valid password
			rai::wallet_value data (entry_get_raw (transaction, key));
			prv.decrypt (data.key, zero_password, salt (transaction).owords [0]);
			rai::public_key compare;
			ed25519_publickey (prv.data.bytes.data (), compare.bytes.data ());
			if (compare == key)
			{
				// If we successfully decrypted it, rewrite the key back with the correct wallet key
				insert_adhoc (transaction, prv);
			}
			else
			{
				// Also try the empty password
				rai::wallet_value data (entry_get_raw (transaction, key));
				prv.decrypt (data.key, empty_password, salt (transaction).owords [0]);
				rai::public_key compare;
				ed25519_publickey (prv.data.bytes.data (), compare.bytes.data ());
				if (compare == key)
				{
					// If we successfully decrypted it, rewrite the key back with the correct wallet key
					insert_adhoc (transaction, prv);
				}
			}
		}
	}
	version_put (transaction, 2);
}

void rai::wallet_store::upgrade_v2_v3 ()
{
	rai::transaction transaction (environment, nullptr, true);
	assert (version (transaction) == 2);
	rai::raw_key seed;
	random_pool.GenerateBlock (seed.data.bytes.data (), seed.data.bytes.size ());
	seed_set (transaction, seed);
	entry_put_raw (transaction, rai::wallet_store::deterministic_index_special, rai::wallet_value (0));
	version_put (transaction, 3);
}

void rai::kdf::phs (rai::raw_key & result_a, std::string const & password_a, rai::uint256_union const & salt_a)
{
	std::lock_guard <std::mutex> lock (mutex);
    auto success (PHS (result_a.data.bytes.data (), result_a.data.bytes.size (), password_a.data (), password_a.size (), salt_a.bytes.data (), salt_a.bytes.size (), 1, rai::wallet_store::kdf_work));
	assert (success == 0); (void) success;
}

rai::wallet::wallet (bool & init_a, rai::transaction & transaction_a, rai::node & node_a, std::string const & wallet_a) :
lock_observer ([](bool, bool){}),
store (init_a, node_a.wallets.kdf, transaction_a, node_a.config.random_representative (), node_a.config.password_fanout, wallet_a),
node (node_a)
{
}

rai::wallet::wallet (bool & init_a, rai::transaction & transaction_a, rai::node & node_a, std::string const & wallet_a, std::string const & json) :
lock_observer ([](bool, bool){}),
store (init_a, node_a.wallets.kdf, transaction_a, node_a.config.random_representative (), node_a.config.password_fanout, wallet_a, json),
node (node_a)
{
}

void rai::wallet::enter_initial_password ()
{
	rai::raw_key password_l;
	store.password.value (password_l);
	if (password_l.data.is_zero ())
	{
		if (valid_password ())
		{
			rai::transaction transaction (store.environment, nullptr, true);
			// Newly created wallets have a zero key
			store.rekey (transaction, "");
		}
		enter_password ("");
	}
}

bool rai::wallet::valid_password ()
{
	rai::transaction transaction (store.environment, nullptr, false);
	auto result (store.valid_password (transaction));
	return result;
}

bool rai::wallet::enter_password (std::string const & password_a)
{
	rai::transaction transaction (store.environment, nullptr, false);
	auto result (store.attempt_password (transaction, password_a));
	if (!result)
	{
		auto this_l (shared_from_this ());
		node.background([this_l] ()
		{
			this_l->search_pending ();
		});
	}
	lock_observer (result, password_a.empty());
	return result;
}

rai::public_key rai::wallet::deterministic_insert ()
{
	rai::block_hash root;
	rai::public_key key (0);
	{
		rai::transaction transaction (store.environment, nullptr, true);
		if (store.valid_password (transaction))
		{
			key = store.deterministic_insert (transaction);
			auto this_l (shared_from_this ());
			root = node.ledger.latest_root (transaction, key);
		}
	}
	if (!key.is_zero ())
	{
		auto this_l (shared_from_this ());
		node.background ([this_l, key, root] () {
			this_l->work_generate (key, root);
		});
	}
	return key;
}

rai::public_key rai::wallet::insert_adhoc (rai::raw_key const & key_a)
{
	rai::block_hash root;
	rai::public_key key (0);
	{
		rai::transaction transaction (store.environment, nullptr, true);
		if (store.valid_password (transaction))
		{
			key = store.insert_adhoc (transaction, key_a);
			auto this_l (shared_from_this ());
			root = node.ledger.latest_root (transaction, key);
		}
	}
	if (!key.is_zero ())
	{
		auto this_l (shared_from_this ());
		node.background ([this_l, key, root] () {
			this_l->work_generate (key, root);
		});
	}
	return key;
}

bool rai::wallet::exists (rai::public_key const & account_a)
{
	rai::transaction transaction (store.environment, nullptr, false);
	return store.exists (transaction, account_a);
}

bool rai::wallet::import (std::string const & json_a, std::string const & password_a)
{
	rai::transaction transaction (store.environment, nullptr, true);
	rai::uint256_union id;
	random_pool.GenerateBlock (id.bytes.data (), id.bytes.size ());
	auto error (false);
	rai::wallet_store temp (error, node.wallets.kdf, transaction, 0, 1, id.to_string (), json_a);
	if (!error)
	{
		error = temp.attempt_password (transaction, password_a);
		if (!error)
		{
			error = store.import (transaction, temp);
		}
	}
	temp.destroy (transaction);
	return error;
}

void rai::wallet::serialize (std::string & json_a)
{
	rai::transaction transaction (store.environment, nullptr, false);
	store.serialize_json (transaction, json_a);
}

void rai::wallet_store::destroy (MDB_txn * transaction_a)
{
	auto status (mdb_drop (transaction_a, handle, 1));
	assert (status == 0);
}

namespace
{
bool check_ownership (rai::wallets & wallets_a, rai::account const & account_a) {
	std::lock_guard <std::mutex> lock (wallets_a.action_mutex);
	return wallets_a.current_actions.find (account_a) == wallets_a.current_actions.end ();
}
}

std::unique_ptr <rai::block> rai::wallet::receive_action (rai::send_block const & send_a, rai::account const & representative_a, rai::uint128_union const & amount_a)
{
    auto hash (send_a.hash ());
	std::unique_ptr <rai::block> block;
	if (node.config.receive_minimum.number () <= amount_a.number ())
	{
		rai::transaction transaction (node.ledger.store.environment, nullptr, false);
		if (node.ledger.store.pending_exists (transaction, hash))
		{
			rai::raw_key prv;
			if (!store.fetch (transaction, send_a.hashables.destination, prv))
			{
				rai::account_info info;
				auto new_account (node.ledger.store.account_get (transaction, send_a.hashables.destination, info));
				if (!new_account)
				{
					auto receive (new rai::receive_block (info.head, hash, prv, send_a.hashables.destination, work_fetch (transaction, send_a.hashables.destination, info.head)));
					block.reset (receive);
				}
				else
				{
					block.reset (new rai::open_block (hash, representative_a, send_a.hashables.destination, prv, send_a.hashables.destination, work_fetch (transaction, send_a.hashables.destination, send_a.hashables.destination)));
				}
			}
			else
			{
				BOOST_LOG (node.log) << "Unable to receive, wallet locked";
			}
		}
		else
		{
			// Ledger doesn't have this marked as available to receive anymore
		}
	}
	else
	{
		BOOST_LOG (node.log) << boost::str (boost::format ("Not receiving block %1% due to minimum receive threshold") % hash.to_string ());
		// Someone sent us something below the threshold of receiving
	}
	if (block != nullptr)
	{
		assert (block != nullptr);
		node.process_receive_republish (block->clone (), node.config.creation_rebroadcast);
		auto hash (block->hash ());
		auto this_l (shared_from_this ());
		auto source (send_a.hashables.destination);
		node.wallets.queue_wallet_action (source, rai::wallets::generate_priority, [this_l, source, hash]
		{
			this_l->work_generate (source, hash);
		});
	}
    return block;
}

std::unique_ptr <rai::block> rai::wallet::change_action (rai::account const & source_a, rai::account const & representative_a)
{
	std::unique_ptr <rai::block> block;
	{
		rai::transaction transaction (store.environment, nullptr, false);
		if (store.valid_password (transaction))
		{
			auto existing (store.find (transaction, source_a));
			if (existing != store.end ())
			{
				if (!node.ledger.latest (transaction, source_a).is_zero ())
				{
					rai::account_info info;
					auto error1 (node.ledger.store.account_get (transaction, source_a, info));
					assert (!error1);
					rai::raw_key prv;
					auto error2 (store.fetch (transaction, source_a, prv));
					assert (!error2);
					block.reset (new rai::change_block (info.head, representative_a, prv, source_a, work_fetch (transaction, source_a, info.head)));
				}
			}
		}
	}
	if (block != nullptr)
	{
		assert (block != nullptr);
		node.process_receive_republish (block->clone (), node.config.creation_rebroadcast);
		auto hash (block->hash ());
		auto this_l (shared_from_this ());
		node.wallets.queue_wallet_action (source_a, rai::wallets::generate_priority, [this_l, source_a, hash]
		{
			this_l->work_generate (source_a, hash);
		});
	}
	return block;
}

std::unique_ptr <rai::block> rai::wallet::send_action (rai::account const & source_a, rai::account const & account_a, rai::uint128_t const & amount_a)
{
	std::unique_ptr <rai::block> block;
	{
		rai::transaction transaction (store.environment, nullptr, false);
		if (store.valid_password (transaction))
		{
			auto existing (store.find (transaction, source_a));
			if (existing != store.end ())
			{
				auto balance (node.ledger.account_balance (transaction, source_a));
				if (!balance.is_zero ())
				{
					if (balance >= amount_a)
					{
						rai::account_info info;
						auto error1 (node.ledger.store.account_get (transaction, source_a, info));
						assert (!error1);
						rai::raw_key prv;
						auto error2 (store.fetch (transaction, source_a, prv));
						assert (!error2);
						block.reset (new rai::send_block (info.head, account_a, balance - amount_a, prv, source_a, work_fetch (transaction, source_a, info.head)));
					}
				}
			}
		}
	}
	if (block != nullptr)
	{
		assert (block != nullptr);
		node.process_receive_republish (block->clone (), node.config.creation_rebroadcast);
		auto hash (block->hash ());
		auto this_l (shared_from_this ());
		node.wallets.queue_wallet_action (source_a, rai::wallets::generate_priority, [this_l, source_a, hash]
		{
			this_l->work_generate (source_a, hash);
		});
	}
	return block;
}

bool rai::wallet::change_sync (rai::account const & source_a, rai::account const & representative_a)
{
	std::promise <bool> result;
	change_async (source_a, representative_a, [this, source_a, representative_a, &result] (std::unique_ptr <rai::block> block_a)
	{
		result.set_value (block_a == nullptr);
	});
	return result.get_future ().get ();
}

void rai::wallet::change_async (rai::account const & source_a, rai::account const & representative_a, std::function <void (std::unique_ptr <rai::block>)> const & action_a)
{
	node.wallets.queue_wallet_action (source_a, rai::wallets::high_priority, [this, source_a, representative_a, action_a] ()
	{
		assert (!check_ownership (node.wallets, source_a));
		auto block (change_action (source_a, representative_a));
		action_a (std::move (block));
	});
}

bool rai::wallet::receive_sync (rai::send_block const & block_a, rai::account const & representative_a, rai::uint128_t const & amount_a)
{
	std::promise <bool> result;
	receive_async (block_a, representative_a, amount_a, [&result] (std::unique_ptr <rai::block> block_a)
	{
		result.set_value (block_a == nullptr);
	});
	return result.get_future ().get ();
}

void rai::wallet::receive_async (rai::send_block const & block_a, rai::account const & representative_a, rai::uint128_t const & amount_a, std::function <void (std::unique_ptr <rai::block>)> const & action_a)
{
	std::shared_ptr <rai::send_block> block_l (static_cast <rai::send_block *> (block_a.clone ().release ()));
	node.wallets.queue_wallet_action (block_a.hashables.destination, amount_a, [this, block_l, representative_a, amount_a, action_a] ()
	{
		assert (!check_ownership (node.wallets, block_l->hashables.destination));
		auto block (receive_action (*block_l, representative_a, amount_a));
		action_a (std::move (block));
	});
}

rai::block_hash rai::wallet::send_sync (rai::account const & source_a, rai::account const & account_a, rai::uint128_t const & amount_a)
{
	std::promise <rai::block_hash> result;
	send_async (source_a, account_a, amount_a, [&result] (std::unique_ptr <rai::block> block_a)
	{
		result.set_value (block_a->hash ());
	});
	return result.get_future ().get ();
}

void rai::wallet::send_async (rai::account const & source_a, rai::account const & account_a, rai::uint128_t const & amount_a, std::function <void (std::unique_ptr <rai::block>)> const & action_a)
{
	node.background ([this, source_a, account_a, amount_a, action_a] ()
	{
		this->node.wallets.queue_wallet_action (source_a, rai::wallets::high_priority, [this, source_a, account_a, amount_a, action_a] ()
		{
			assert (!check_ownership (node.wallets, source_a));
			auto block (send_action (source_a, account_a, amount_a));
			action_a (std::move (block));
		});
	});
}

// Update work for account if latest root is root_a
void rai::wallet::work_update (MDB_txn * transaction_a, rai::account const & account_a, rai::block_hash const & root_a, uint64_t work_a)
{
    assert (!node.work.work_validate (root_a, work_a));
    assert (store.exists (transaction_a, account_a));
    auto latest (node.ledger.latest_root (transaction_a, account_a));
    if (latest == root_a)
    {
        store.work_put (transaction_a, account_a, work_a);
    }
    else
    {
        BOOST_LOG (node.log) << "Cached work no longer valid, discarding";
    }
}

// Fetch work for root_a, use cached value if possible
uint64_t rai::wallet::work_fetch (MDB_txn * transaction_a, rai::account const & account_a, rai::block_hash const & root_a)
{
    uint64_t result;
    auto error (store.work_get (transaction_a, account_a, result));
    if (error)
	{
        result = node.generate_work (root_a);
    }
	else
	{
		if (node.work.work_validate (root_a, result))
		{
			BOOST_LOG (node.log) << "Cached work invalid, regenerating";
			result = node.generate_work (root_a);
		}
	}
    return result;
}

namespace
{
class search_action : public std::enable_shared_from_this <search_action>
{
public:
	search_action (std::shared_ptr <rai::wallet> const & wallet_a, MDB_txn * transaction_a) :
	wallet (wallet_a)
	{
		for (auto i (wallet_a->store.begin (transaction_a)), n (wallet_a->store.end ()); i != n; ++i)
		{
			keys.insert (i->first);
		}
	}
	void run ()
	{
		BOOST_LOG (wallet->node.log) << "Beginning pending block search";
		rai::transaction transaction (wallet->node.store.environment, nullptr, false);
		std::unordered_set <rai::account> already_searched;
		for (auto i (wallet->node.store.pending_begin (transaction)), n (wallet->node.store.pending_end ()); i != n; ++i)
		{
			rai::receivable receivable (i->second);
			auto existing (keys.find (receivable.destination));
			if (existing != keys.end ())
			{
				rai::account_info info;
				wallet->node.store.account_get (transaction, receivable.source, info);
				BOOST_LOG (wallet->node.log) << boost::str (boost::format ("Found a pending block %1% from account %2% with head %3%") % receivable.source.to_string () % receivable.source.to_account () % info.head.to_string ());
				auto account (receivable.source);
				if (already_searched.find (account) == already_searched.end ())
				{
					auto this_l (shared_from_this ());
					std::shared_ptr <rai::block> block_l (wallet->node.store.block_get (transaction, info.head).release ());
					wallet->node.background ([this_l, account, block_l]
					{
						this_l->wallet->node.active.start (*block_l, [this_l, account] (rai::block &)
						{
							// If there were any forks for this account they've been rolled back and we can receive anything remaining from this account
							this_l->receive_all (account);
						});
						this_l->wallet->node.network.broadcast_confirm_req (*block_l);
					});
					already_searched.insert (account);
				}
			}
		}
		BOOST_LOG (wallet->node.log) << "Pending block search phase complete";
	}
	void receive_all (rai::account const & account_a)
	{
		BOOST_LOG (wallet->node.log) << boost::str (boost::format ("Account %1% confirmed, receiving all blocks") % account_a.to_account ());
		rai::transaction transaction (wallet->node.store.environment, nullptr, false);
		auto representative (wallet->store.representative (transaction));
		for (auto i (wallet->node.store.pending_begin (transaction)), n (wallet->node.store.pending_end ()); i != n; ++i)
		{
			rai::receivable receivable (i->second);
			if (receivable.source == account_a)
			{
				if (wallet->store.exists (transaction, receivable.destination))
				{
					if (wallet->store.valid_password (transaction))
					{
						auto block_l (wallet->node.store.block_get (transaction, i->first));
						assert (dynamic_cast <rai::send_block *> (block_l.get ()) != nullptr);
						std::shared_ptr <rai::send_block> block (static_cast <rai::send_block *> (block_l.release ()));
						auto wallet_l (wallet);
						auto amount (receivable.amount.number ());
						BOOST_LOG (wallet_l->node.log) << boost::str (boost::format ("Receiving block: %1%") % block->hash ().to_string ());
						wallet_l->receive_async (*block, representative, amount, [wallet_l, block] (std::unique_ptr <rai::block> block_a)
						{
							if (block_a == nullptr)
							{
								BOOST_LOG (wallet_l->node.log) << boost::str (boost::format ("Error receiving block %1%") % block->hash ().to_string ());
							}
						});
					}
					else
					{
						BOOST_LOG (wallet->node.log) << boost::str (boost::format ("Unable to fetch key for: %1%, stopping pending search") % receivable.destination.to_account ());
					}
				}
			}
		}
	}
	std::unordered_set <rai::uint256_union> keys;
	std::shared_ptr <rai::wallet> wallet;
};
}

bool rai::wallet::search_pending ()
{
	rai::transaction transaction (store.environment, nullptr, false);
	auto result (!store.valid_password (transaction));
	if (!result)
	{
		auto search (std::make_shared <search_action> (shared_from_this (), transaction));
		node.background ([search] ()
		{
			search->run ();
		});
	}
	else
	{
		BOOST_LOG (node.log) << "Stopping search, wallet is locked";
	}
	return result;
}

void rai::wallet::init_free_accounts (MDB_txn * transaction_a)
{
	free_accounts.clear ();
	for (auto i (store.begin (transaction_a)), n (store.end ()); i != n; ++i)
	{
		free_accounts.insert (i->first);
	}
}

void rai::wallet::work_generate (rai::account const & account_a, rai::block_hash const & root_a)
{
	auto begin (std::chrono::system_clock::now ());
    auto work (node.generate_work (root_a));
	if (node.config.logging.work_generation_time ())
	{
		BOOST_LOG (node.log) << "Work generation complete: " << (std::chrono::duration_cast <std::chrono::microseconds> (std::chrono::system_clock::now () - begin).count ()) << " us";
	}
	rai::transaction transaction (store.environment, nullptr, true);
	if (store.exists (transaction, account_a))
	{
		work_update (transaction, account_a, root_a, work);
	}
}

rai::wallets::wallets (bool & error_a, rai::node & node_a) :
observer ([] (rai::account const &, bool) {}),
node (node_a)
{
	if (!error_a)
	{
		rai::transaction transaction (node.store.environment, nullptr, true);
		auto status (mdb_dbi_open (transaction, nullptr, MDB_CREATE, &handle));
		assert (status == 0);
		std::string beginning (rai::uint256_union (0).to_string ());
		std::string end ((rai::uint256_union (rai::uint256_t (0) - rai::uint256_t (1))).to_string ());
		for (rai::store_iterator i (transaction, handle, rai::mdb_val (beginning.size (), const_cast <char *> (beginning.c_str ()))), n (transaction, handle, rai::mdb_val (end.size (), const_cast <char *> (end.c_str ()))); i != n; ++i)
		{
			rai::uint256_union id;
			std::string text (reinterpret_cast <char const *> (i->first.mv_data), i->first.mv_size);
			auto error (id.decode_hex (text));
			assert (!error);
			assert (items.find (id) == items.end ());
			auto wallet (std::make_shared <rai::wallet> (error, transaction, node_a, text));
			if (!error)
			{
				node_a.background ([wallet] ()
				{
					wallet->enter_initial_password ();
				});
				items [id] = wallet;
			}
			else
			{
				// Couldn't open wallet
			}
		}
	}
}

std::shared_ptr <rai::wallet> rai::wallets::open (rai::uint256_union const & id_a)
{
    std::shared_ptr <rai::wallet> result;
    auto existing (items.find (id_a));
    if (existing != items.end ())
    {
        result = existing->second;
    }
    return result;
}

std::shared_ptr <rai::wallet> rai::wallets::create (rai::uint256_union const & id_a)
{
    assert (items.find (id_a) == items.end ());
    std::shared_ptr <rai::wallet> result;
    bool error;
	{
		rai::transaction transaction (node.store.environment, nullptr, true);
		result = std::make_shared <rai::wallet> (error, transaction, node, id_a.to_string ());
        items [id_a] = result;
        result = result;
	}
    if (!error)
    {
		node.background ([result] ()
		{
			result->enter_initial_password ();
		});
    }
    return result;
}

bool rai::wallets::search_pending (rai::uint256_union const & wallet_a)
{
	auto result (false);
	auto existing (items.find (wallet_a));
	result = existing == items.end ();
	if (!result)
	{
		auto wallet (existing->second);
		result = wallet->search_pending ();
	}
	return result;
}

void rai::wallets::search_pending_all ()
{
	for (auto i: items)
	{
		i.second->search_pending ();
	}
}

void rai::wallets::destroy (rai::uint256_union const & id_a)
{
	rai::transaction transaction (node.store.environment, nullptr, true);
	auto existing (items.find (id_a));
	assert (existing != items.end ());
	auto wallet (existing->second);
	items.erase (existing);
	wallet->store.destroy (transaction);
}

void rai::wallets::do_wallet_actions (rai::account const & account_a)
{
	observer (account_a, true);
	std::unique_lock <std::mutex> lock (node.wallets.action_mutex);
	auto existing (node.wallets.pending_actions.find (account_a));
	while (existing != node.wallets.pending_actions.end ())
	{
		auto & entries (existing->second);
		if (entries.empty ())
		{
			node.wallets.pending_actions.erase (existing);
			auto erased (node.wallets.current_actions.erase (account_a));
			assert (erased == 1); (void) erased;
		}
		else
		{
			auto first (entries.begin ());
			auto current (std::move (first->second));
			entries.erase (first);
			lock.unlock ();
			current ();
			lock.lock ();
		}
		existing = node.wallets.pending_actions.find (account_a);
	}
	observer (account_a, false);
}

void rai::wallets::queue_wallet_action (rai::account const & account_a, rai::uint128_t const & amount_a, std::function <void ()> const & action_a)
{
	std::lock_guard <std::mutex> lock (action_mutex);
	pending_actions [account_a].insert (decltype (pending_actions)::mapped_type::value_type (amount_a, std::move (action_a)));
	if (current_actions.insert (account_a).second)
	{
		auto node_l (node.shared ());
		node.background ([node_l, account_a] ()
		{
			node_l->wallets.do_wallet_actions (account_a);
		});
	}
}

void rai::wallets::foreach_representative (std::function <void (rai::public_key const & pub_a, rai::raw_key const & prv_a)> const & action_a)
{
    for (auto i (items.begin ()), n (items.end ()); i != n; ++i)
    {
		rai::transaction transaction (node.store.environment, nullptr, false);
        auto & wallet (*i->second);
		for (auto j (wallet.store.begin (transaction)), m (wallet.store.end ()); j != m; ++j)
        {
			rai::account account (j->first);
			if (!node.ledger.weight (transaction, account).is_zero ())
			{
				if (wallet.store.valid_password (transaction))
				{
					rai::raw_key prv;
					auto error (wallet.store.fetch (transaction, j->first, prv));
					assert (!error);
					action_a (j->first, prv);
				}
				else
				{
					BOOST_LOG (node.log) << boost::str (boost::format ("Skipping locked wallet %1% with account %2%") % i->first.to_string () % account.to_account ());
				}
			}
        }
    }
}

rai::uint128_t const rai::wallets::generate_priority = std::numeric_limits <rai::uint128_t>::max ();
rai::uint128_t const rai::wallets::high_priority = std::numeric_limits <rai::uint128_t>::max () - 1;

rai::store_iterator rai::wallet_store::begin (MDB_txn * transaction_a)
{
    rai::store_iterator result (transaction_a, handle, rai::uint256_union (special_count).val ());
    return result;
}

rai::store_iterator rai::wallet_store::begin (MDB_txn * transaction_a, rai::uint256_union const & key)
{
    rai::store_iterator result (transaction_a, handle, key.val ());
	return result;
}

rai::store_iterator rai::wallet_store::find (MDB_txn * transaction_a, rai::uint256_union const & key)
{
	auto result (begin (transaction_a, key));
    rai::store_iterator end (nullptr);
    if (result != end)
    {
        if (rai::uint256_union (result->first) == key)
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

rai::store_iterator rai::wallet_store::end ()
{
    return rai::store_iterator (nullptr);
}
