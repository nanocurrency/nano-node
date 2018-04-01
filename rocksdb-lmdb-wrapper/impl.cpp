#include <mutex>

#include <boost/endian/conversion.hpp>
#include <boost/optional.hpp>

#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include "rocksdb/slice.h"
#include "rocksdb/utilities/optimistic_transaction_db.h"
#include "rocksdb/utilities/transaction.h"

#include "lmdb.h"

using namespace rocksdb;

struct MDB_env
{
	DB * db;
	OptimisticTransactionDB * txn_db;
	std::mutex write_mutex;
};

int mdb_env_create (MDB_env ** env)
{
	*env = new MDB_env ();
	(*env)->db = nullptr;
	(*env)->txn_db = nullptr;
	return 0;
}

int mdb_env_set_maxdbs (MDB_env *, unsigned int dbs)
{
	return dbs >= (1 << 15);
}

int mdb_env_set_mapsize (MDB_env *, size_t size)
{
	return 0;
}

int mdb_env_open (MDB_env * env, const char * path, unsigned int flags, uint32_t mode)
{
	Options options;
	options.create_if_missing = true;
	OptimisticTransactionDB * txn_db;

	int result (OptimisticTransactionDB::Open (options, path, &txn_db).code ());
	env->txn_db = txn_db;
	env->db = txn_db->GetBaseDB ();
	return result;
}

int mdb_env_copy2 (MDB_env *, const char * path, unsigned int flags)
{
	return 1;
}

void mdb_env_close (MDB_env * env)
{
	if (env->txn_db)
	{
		delete env->txn_db;
	}
	delete env;
}

struct MDB_txn
{
	DB * db;
	boost::optional<std::unique_lock<std::mutex>> write_guard;
	Transaction * write_txn;
	ReadOptions read_opts;
};

namespace
{
Status txn_get (MDB_txn * txn, const Slice & key, std::string * value)
{
	Status result;
	if (txn->write_txn)
	{
		result = txn->write_txn->Get (txn->read_opts, key, value);
	}
	else
	{
		result = txn->db->Get (txn->read_opts, key, value);
	}
	return result;
}

std::vector<uint8_t> namespace_key (MDB_val * val, MDB_dbi dbi)
{
	uint8_t * dbi_bytes = (uint8_t *)&dbi;
	std::vector<uint8_t> buf;
	buf.push_back (dbi_bytes[0]);
	buf.push_back (dbi_bytes[1]);
	const char * data_ptr ((const char *)val->mv_data);
	std::copy (data_ptr, data_ptr + val->mv_size, std::back_inserter (buf));
	return buf;
}

void string_to_val (std::string str, MDB_val * val)
{
	val->mv_size = str.size ();
	val->mv_data = malloc (val->mv_size);
	std::memcpy (val->mv_data, str.data (), val->mv_size);
}
}

int mdb_txn_begin (MDB_env * env, MDB_txn *, unsigned int flags, MDB_txn ** txn)
{
	*txn = new MDB_txn ();
	(*txn)->db = env->db;
	if ((flags & MDB_RDONLY) != MDB_RDONLY)
	{
		std::unique_lock<std::mutex> write_guard (env->write_mutex);
		(*txn)->write_guard = std::move (write_guard);
		(*txn)->write_txn = env->txn_db->BeginTransaction (WriteOptions ());
		// we don't need a snapshot since we already have a mutex lock
	}
	else
	{
		(*txn)->read_opts.snapshot = env->db->GetSnapshot ();
		(*txn)->write_txn = nullptr;
	}
	return 0;
}

int mdb_txn_commit (MDB_txn * txn)
{
	int result (0);
	if (txn->write_txn)
	{
		result = txn->write_txn->Commit ().code ();
	}
	delete txn;
	return result;
}

const uint16_t INTERNAL_PREFIX_FLAG = 1 << 15;
const uint16_t DBI_LOOKUP_PREFIX = INTERNAL_PREFIX_FLAG | 0x1;
const uint16_t NEXT_DBI_KEY = INTERNAL_PREFIX_FLAG | 0x2;

int mdb_dbi_open (MDB_txn * txn, const char * name, unsigned int flags, MDB_dbi * dbi)
{
	union
	{
		uint16_t prefix_int;
		std::array<uint8_t, 2> prefix_bytes;
	};
	prefix_int = DBI_LOOKUP_PREFIX;
	boost::endian::native_to_little_inplace (prefix_int);
	std::vector<uint8_t> dbi_lookup_key_bytes (prefix_bytes.begin (), prefix_bytes.end ());
	if (name == nullptr)
	{
		name = "";
	}
	std::string name_str (name);
	std::copy (name_str.begin (), name_str.end (), std::back_inserter (dbi_lookup_key_bytes));
	Slice dbi_lookup_key (Slice ((const char *)dbi_lookup_key_bytes.data (), dbi_lookup_key_bytes.size ()));
	std::string dbi_buf;
	int result (txn_get (txn, dbi_lookup_key, &dbi_buf).code ());
	if (!result && dbi_buf.size () != 2)
	{
		result = MDB_CORRUPTED;
	}
	else if (result == MDB_NOTFOUND)
	{
		Slice next_dbi_key (Slice ((const char *)&NEXT_DBI_KEY, sizeof (NEXT_DBI_KEY)));
		std::string next_dbi_buf;
		result = txn_get (txn, next_dbi_key, &next_dbi_buf).code ();
		if (!result && next_dbi_buf.size () != 2)
		{
			result = MDB_CORRUPTED;
		}
		else if (result == MDB_NOTFOUND && txn->write_txn)
		{
			result = 0;
			const char zero_dbi[] = { 0, 0 };
			next_dbi_buf = std::string (zero_dbi, sizeof (zero_dbi));
		}
		if (!result)
		{
			dbi_buf = std::string (next_dbi_buf);
			// modifying a string's .data() is not technically allowed,
			// so we're doing a bit of manual addition here
#if defined(BOOST_LITTLE_ENDIAN)
			uint8_t indicies[] = { 0, 1 };
#elif defined(BOOST_BIG_ENDIAN)
			uint8_t indicies[] = { 1, 0 };
#endif
			next_dbi_buf[indicies[0]] += 1;
			if (next_dbi_buf[indicies[0]] == 0) // overflow
			{
				next_dbi_buf[indicies[1]] += 1;
			}
			result = txn->write_txn->Put (next_dbi_key, next_dbi_buf).code ();
			if (!result)
			{
				result = txn->write_txn->Put (dbi_lookup_key, next_dbi_buf).code ();
			}
		}
	}
	if (!result)
	{
		uint8_t * dbi_bytes = (uint8_t *)dbi;
		dbi_bytes[0] = dbi_buf[0];
		dbi_bytes[1] = dbi_buf[1];
	}
	return result;
}

void mdb_dbi_close (MDB_env *, MDB_dbi)
{
	// We don't use true handles, so we have nothing to do here
}

int mdb_drop (MDB_txn *, MDB_dbi, int del)
{
	// TODO
	return del;
}

int mdb_get (MDB_txn * txn, MDB_dbi dbi, MDB_val * key, MDB_val * value)
{
	std::string out_buf;
	std::vector<uint8_t> namespaced_key (namespace_key (key, dbi));
	int result (txn_get (txn, Slice ((const char *)namespaced_key.data (), namespaced_key.size ()), &out_buf).code ());
	if (!result)
	{
		string_to_val (out_buf, value);
	}
	return result;
}

int mdb_put (MDB_txn * txn, MDB_dbi dbi, MDB_val * key, MDB_val * value, unsigned int flags)
{
	int result (0);
	if (!txn->write_txn)
	{
		result = MDB_BAD_TXN;
	}
	if (!result)
	{
		std::vector<uint8_t> namespaced_key (namespace_key (key, dbi));
		result = txn->write_txn->Put (Slice ((const char *)namespaced_key.data (), namespaced_key.size ()), Slice ((const char *)value->mv_data, value->mv_size)).code ();
	}
	return result;
}

int mdb_del (MDB_txn * txn, MDB_dbi dbi, MDB_val * key, MDB_val * value)
{
	int result = 0;
	if (!txn->write_txn)
	{
		result = MDB_BAD_TXN;
	}
	if (!result)
	{
		std::vector<uint8_t> namespaced_key (namespace_key (key, dbi));
		result = txn->write_txn->Delete (Slice ((const char *)namespaced_key.data (), namespaced_key.size ())).code ();
	}
	return result;
}

struct MDB_cursor
{
	MDB_dbi dbi;
	Iterator * it;
	Transaction * write_txn;
};

int mdb_cursor_open (MDB_txn * txn, MDB_dbi dbi, MDB_cursor ** cursor)
{
	int result = 0;
	*cursor = new MDB_cursor ();
	(*cursor)->dbi = dbi;
	if (txn->write_txn)
	{
		(*cursor)->it = txn->write_txn->GetIterator (txn->read_opts);
	}
	else
	{
		(*cursor)->it = txn->db->NewIterator (txn->read_opts);
	}
	(*cursor)->write_txn = txn->write_txn;
	return ((*cursor)->it == nullptr) ? MDB_PANIC : 0;
}

int mdb_cursor_get (MDB_cursor * cursor, MDB_val * key, MDB_val * value, MDB_cursor_op op)
{
	int result (0);
	bool seeked (false);
	switch (op)
	{
		case MDB_GET_CURRENT:
		{
			if (!cursor->it->Valid ())
			{
				result = MDB_NOTFOUND;
			}
			else if (!cursor->it->status ().ok ())
			{
				result = cursor->it->status ().code ();
			}
			else
			{
				Slice key_slice (cursor->it->key ());
				if (key_slice.size () < 2)
				{
					result = MDB_CORRUPTED;
				}
				else
				{
					key->mv_size = key_slice.size () - 2;
					key->mv_data = malloc (key->mv_size);
					std::memcpy (key->mv_data, key_slice.data () + 2, key->mv_size);
					Slice value_slice (cursor->it->value ());
					value->mv_size = value_slice.size ();
					value->mv_data = malloc (value->mv_size);
					std::memcpy (value->mv_data, value_slice.data (), value->mv_size);
				}
			}
			break;
		}
		case MDB_FIRST:
			cursor->it->Seek (Slice ((const char *)&cursor->dbi, sizeof (cursor->dbi)));
			seeked = true;
			break;
		case MDB_SET_RANGE:
		{
			std::vector<uint8_t> ns_key (namespace_key (key, cursor->dbi));
			cursor->it->Seek (Slice ((const char *)ns_key.data (), ns_key.size ()));
			seeked = true;
			break;
		}
		case MDB_NEXT:
			if (cursor->it->Valid ())
			{
				cursor->it->Next ();
			}
			else
			{
				result = MDB_NOTFOUND;
			}
			seeked = true;
			break;
		case MDB_NEXT_DUP:
			result = MDB_NOTFOUND;
			break;
	}
	if (seeked && !result)
	{
		Slice key (cursor->it->key ());
		if (key.size () < 2)
		{
			result = MDB_CORRUPTED;
		}
		else
		{
			if (*((uint16_t *)key.data ()) != cursor->dbi)
			{
				result = MDB_NOTFOUND;
			}
		}
	}
	if (!result)
	{
		result = cursor->it->status ().code ();
	}
	return result;
}

int mdb_cursor_put (MDB_cursor * cursor, MDB_val * key, MDB_val * value, unsigned int flags)
{
	int result (0);
	if (cursor->write_txn)
	{
		Slice key_slice ((const char *)key->mv_data, key->mv_size);
		cursor->write_txn->Put (key_slice, Slice ((const char *)value->mv_data, value->mv_size));
		cursor->it->Seek (key_slice);
	}
	else
	{
		result = MDB_BAD_TXN;
	}
	return result;
}

void mdb_cursor_close (MDB_cursor * cursor)
{
	delete cursor->it;
}

int mdb_stat (MDB_txn *, MDB_dbi, MDB_stat * stat)
{
	stat->ms_entries = 1; // TODO
	return 0;
}
