#include "stddef.h"
#include "stdint.h"

struct MDB_env;

int mdb_env_create (MDB_env **);
int mdb_env_set_maxdbs (MDB_env *, unsigned int dbs);
int mdb_env_set_mapsize (MDB_env *, size_t size);
int mdb_env_open (MDB_env *, const char *, unsigned int, uint32_t);

struct MDB_txn;

int mdb_txn_begin (MDB_env *, MDB_txn *, unsigned int, MDB_txn **);
int mdb_txn_commit (MDB_txn *);

typedef uint16_t MDB_dbi;

int mdb_dbi_open (MDB_txn *, const char *, unsigned int, MDB_dbi *);
void mdb_dbi_close (MDB_env *, MDB_dbi);

struct MDB_val {
    size_t mv_size;
    void * mv_data;
};

int mdb_get (MDB_txn *, MDB_dbi, MDB_val *, MDB_val *);
int mdb_put (MDB_txn *, MDB_dbi, MDB_val *, MDB_val *, unsigned int);
int mdb_del (MDB_txn *, MDB_dbi, MDB_val *, MDB_val *);

struct MDB_cursor;

enum MDB_cursor_op {
    MDB_FIRST,
    MDB_GET_CURRENT,
    MDB_SET_RANGE
};

int mdb_cursor_open (MDB_txn *, MDB_dbi, MDB_cursor **);
int mdb_cursor_get (MDB_cursor *, MDB_val *, MDB_val *, MDB_cursor_op);
void mdb_cursor_close (MDB_cursor *);

extern const int MDB_NOTFOUND;
