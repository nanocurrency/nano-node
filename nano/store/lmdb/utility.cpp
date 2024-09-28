#include <nano/store/lmdb/utility.hpp>
#include <nano/store/transaction.hpp>

MDB_txn * nano::store::lmdb::tx (store::transaction const & transaction_a)
{
	return static_cast<MDB_txn *> (transaction_a.get_handle ());
}
