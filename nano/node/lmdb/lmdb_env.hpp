#pragma once

#include <nano/node/lmdb/lmdb_txn.hpp>
#include <nano/secure/blockstore.hpp>

namespace nano
{
/**
 * RAII wrapper for MDB_env
 */
class mdb_env final
{
public:
	mdb_env (bool &, boost::filesystem::path const &, int max_dbs = 128, bool use_no_mem_init = false, size_t map_size = 128ULL * 1024 * 1024 * 1024);
	void init (bool &, boost::filesystem::path const &, int max_dbs, bool use_no_mem_init, size_t map_size = 128ULL * 1024 * 1024 * 1024);
	~mdb_env ();
	operator MDB_env * () const;
	// clang-format off
	nano::read_transaction tx_begin_read (mdb_txn_callbacks txn_callbacks = mdb_txn_callbacks{}) const;
	nano::write_transaction tx_begin_write (mdb_txn_callbacks txn_callbacks = mdb_txn_callbacks{}) const;
	MDB_txn * tx (nano::transaction const & transaction_a) const;
	// clang-format on
	MDB_env * environment;
};
}
