#include <nano/store/rocksdb/utility.hpp>
#include <nano/store/rocksdb/transaction_impl.hpp>

auto nano::store::rocksdb::tx (store::transaction const & transaction_a) -> std::variant<::rocksdb::Transaction *, ::rocksdb::ReadOptions *>
{
	if (dynamic_cast<nano::store::read_transaction const *> (&transaction_a) != nullptr)
	{
		return static_cast<::rocksdb::ReadOptions *> (transaction_a.get_handle ());
	}
	return static_cast<::rocksdb::Transaction *> (transaction_a.get_handle ());
}
