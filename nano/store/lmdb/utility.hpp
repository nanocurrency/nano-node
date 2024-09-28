#include <lmdb/libraries/liblmdb/lmdb.h>

namespace nano::store
{
class transaction;
}
namespace nano::store::lmdb
{
MDB_txn * tx (store::transaction const & transaction_a);
}
