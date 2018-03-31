#include <mutex>

#include <boost/optional.hpp>
#include <boost/endian/conversion.hpp>

#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include "rocksdb/slice.h"
#include "rocksdb/utilities/transaction.h"
#include "rocksdb/utilities/optimistic_transaction_db.h"

#include "lmdb.h"

using namespace rocksdb;

struct MDB_env {
    DB * db;
    OptimisticTransactionDB * txn_db;
    std::mutex write_mutex;
};

int mdb_env_create (MDB_env ** env) {
    *env = new MDB_env();
    (*env)->db = nullptr;
    (*env)->txn_db = nullptr;
}

int mdb_env_set_maxdbs (MDB_env *, unsigned int dbs) {
    return dbs >= (1 >> 15);
}

int mdb_env_set_mapsize (MDB_env *, size_t size) {
    return 0;
}

int mdb_env_open (MDB_env * env, const char * path, unsigned int flags, uint32_t mode) {
    Options options;
    options.create_if_missing = true;
    OptimisticTransactionDB * txn_db;

    int result = OptimisticTransactionDB::Open (options, path, &txn_db).code ();
    env->txn_db = txn_db;
    env->db = txn_db->GetBaseDB ();
    return result;
}

int mdb_env_copy2 (MDB_env *, const char * path, unsigned int flags) {
    return 1;
}

void mdb_env_close (MDB_env * env) {
    if (env->txn_db)
    {
        delete env->txn_db;
    }
    delete env;
}

struct MDB_txn {
    DB * db;
    boost::optional<std::lock_guard<std::mutex>> write_guard;
    Transaction * write_txn;
    ReadOptions read_opts;
};

namespace {
Status txn_get (MDB_txn * txn, const Slice & key, std::string * value) {
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

std::vector<uint8_t> namespace_key (MDB_val * val, MDB_dbi dbi) {
    uint8_t * dbi_bytes = (uint8_t *) &dbi;
    std::vector<uint8_t> buf;
    buf.push_back (dbi_bytes[0]);
    buf.push_back (dbi_bytes[1]);
    std::copy (val->mv_data, val->mv_data + val->mv_size, buf);
    return buf;
}
}

int mdb_txn_begin (MDB_env * env, MDB_txn *, unsigned int flags, MDB_txn ** txn) {
    *txn = new MDB_txn ();
    (*txn)->db = env->db;
    if ((flags & MDB_RDONLY) != MDB_RDONLY)
    {
        std::lock_guard<std::mutex> write_guard (env->write_mutex);
        (*txn)->write_guard = write_guard;
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

int mdb_txn_commit (MDB_txn * txn) {
    int result = 0;
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

int mdb_dbi_open (MDB_txn * txn, const char * name, unsigned int flags, MDB_dbi * dbi) {
    union {
        uint16_t prefix_int;
        std::array<uint8_t, 2> prefix_bytes;
    };
    prefix_int = DBI_LOOKUP_PREFIX;
    boost::endian::native_to_little_inplace (prefix_int);
    std::vector<uint8_t> dbi_lookup_key_bytes (prefix_bytes.begin (), prefix_bytes.end ());
    dbi_lookup_key_bytes.insert (dbi_lookup_key_bytes.end (), name, name + strlen(name));
    Slice dbi_lookup_key (Slice ((const char *) dbi_lookup_key.data (), dbi_lookup_key.size ()));
    std::string dbi_buf;
    int result = txn_get (txn, dbi_lookup_key, &dbi_buf).code ();
    if (!result && dbi_buf.size () != 2)
    {
        result = MDB_CORRUPTED;
    }
    else if (result == MDB_NOTFOUND)
    {
        Slice next_dbi_key (Slice ((const char *) &NEXT_DBI_KEY, sizeof (NEXT_DBI_KEY)));
        std::string next_dbi_buf;
        result = txn_get (txn, next_dbi_key, &next_dbi_buf).code ();
        if (!result && dbi_buf.size () != 2)
        {
            result = MDB_CORRUPTED;
        }
        else if (result == MDB_NOTFOUND && txn->write_txn)
        {
            next_dbi_buf = "\x00\x00";
        }
        if (!result)
        {
            dbi_buf = std::string (next_dbi_buf);
            // modifying a string's .data() is not technically allowed,
            // so we're doing a bit of manual addition here
#if defined(BOOST_LITTLE_ENDIAN)
            uint8_t indicies[] = {0, 1};
#elif defined(BOOST_BIG_ENDIAN)
            uint8_t indicies[] = {1, 0};
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
        uint8_t * dbi_bytes = (uint8_t *) dbi;
        dbi_bytes[0] = dbi_buf[0];
        dbi_bytes[1] = dbi_buf[1];
    }
    return result;
}

void mdb_dbi_close (MDB_env *, MDB_dbi) {
    // We don't use true handles, so we have nothing to do here
}

int mdb_drop (MDB_txn *, MDB_dbi, int del) {
    // TODO
    return del;
}

int mdb_get (MDB_txn * txn, MDB_dbi dbi, MDB_val * key, MDB_val * value) {
    std::string out_buf;
    std::vector<uint8_t> namespaced_key (namespace_key (key, dbi));
    int result = txn_get (txn, Slice ((const char *) namespaced_key.data (), namespaced_key.size ()), &out_buf).code ();
    if (result == 0)
    {
        value->mv_size = out_buf.size ();
        value->mv_data = malloc (out_buf.size ());
        std::copy (out_buf.begin (), out_buf.end (), value->mv_data);
    }
    return result;
}

int mdb_put (MDB_txn * txn, MDB_dbi dbi, MDB_val * key, MDB_val * value, unsigned int flags) {
    int result = 0;
    if (!txn->write_txn)
    {
        result = MDB_BAD_TXN;
    }
    if (result == 0)
    {
        std::vector<uint8_t> namespaced_key (namespace_key (key, dbi));
        result = txn->write_txn->Put (Slice ((const char *) namespaced_key.data (), namespaced_key.size ()), Slice ((const char *) value->mv_data, value->mv_size)).code ();
    }
    return result;
}

int mdb_del (MDB_txn * txn, MDB_dbi dbi, MDB_val * key, MDB_val * value) {
    int result = 0;
    if (!txn->write_txn)
    {
        result = MDB_BAD_TXN;
    }
    if (result == 0)
    {
        std::vector<uint8_t> namespaced_key (namespace_key (key, dbi));
        result = txn->write_txn->Delete (Slice ((const char *) namespaced_key.data (), namespaced_key.size ())).code ();
    }
    return result;
}
