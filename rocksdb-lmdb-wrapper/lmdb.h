#pragma once

#include "stddef.h"
#include "stdint.h"

#define MDB_CP_COMPACT 0x01

#define MDB_DUPSORT 0x04
#define MDB_CURRENT 0x40
#define MDB_CREATE 0x40000

#define MDB_NOSUBDIR 0x4000
#define MDB_RDONLY 0x20000
#define MDB_NOTLS 0x200000

#define MDB_NOTFOUND 1 // from RocksDB
#define MDB_PANIC 0x80 // we use this when we don't know what the error is
#define MDB_BAD_TXN 0x81
#define MDB_CORRUPTED 0x82

struct MDB_env;

int mdb_env_create (MDB_env **);
int mdb_env_set_maxdbs (MDB_env *, unsigned int dbs);
int mdb_env_set_mapsize (MDB_env *, size_t size);
int mdb_env_open (MDB_env *, const char * path, unsigned int flags, uint32_t mode);
int mdb_env_copy2 (MDB_env *, const char * path, unsigned int flags);
void mdb_env_close (MDB_env *);

struct MDB_txn;

int mdb_txn_begin (MDB_env *, MDB_txn *, unsigned int flags, MDB_txn **);
int mdb_txn_commit (MDB_txn *);

typedef uint16_t MDB_dbi;

int mdb_dbi_open (MDB_txn *, const char * name, unsigned int flags, MDB_dbi *);
void mdb_dbi_close (MDB_env *, MDB_dbi);
int mdb_drop (MDB_txn *, MDB_dbi, int del);

struct MDB_val
{
	size_t mv_size;
	void * mv_data;
};

int mdb_get (MDB_txn *, MDB_dbi, MDB_val * key, MDB_val * value);
int mdb_put (MDB_txn *, MDB_dbi, MDB_val * key, MDB_val * value, unsigned int flags);
int mdb_del (MDB_txn *, MDB_dbi, MDB_val * key, MDB_val * value);

struct MDB_cursor;

enum MDB_cursor_op
{
	MDB_FIRST,
	MDB_GET_CURRENT,
	MDB_SET_RANGE,
	MDB_NEXT,
	MDB_NEXT_DUP
};

int mdb_cursor_open (MDB_txn *, MDB_dbi, MDB_cursor **);
int mdb_cursor_get (MDB_cursor *, MDB_val * key, MDB_val * data, MDB_cursor_op);
int mdb_cursor_put (MDB_cursor *, MDB_val * key, MDB_val * data, unsigned int flags);
void mdb_cursor_close (MDB_cursor *);

struct MDB_stat
{
	/*
    unsigned int ms_psize;
    unsigned int ms_depth;
    size_t ms_branch_pages;
    size_t ms_leaf_pages;
    size_t ms_overflow_pages;
    */
	size_t ms_entries;
};

int mdb_stat (MDB_txn *, MDB_dbi, MDB_stat *);
