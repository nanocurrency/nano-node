#include <nano/store/rocksdb/utility.hpp>
#include <nano/store/rocksdb/transaction_impl.hpp>

auto nano::store::rocksdb::is_read (nano::store::transaction const & transaction_a) -> bool
{
	return (dynamic_cast<nano::store::read_transaction const *> (&transaction_a) != nullptr);
}

auto nano::store::rocksdb::tx (store::transaction const & transaction_a) -> ::rocksdb::Transaction *
{
	debug_assert (!is_read (transaction_a));
	return static_cast<::rocksdb::Transaction *> (transaction_a.get_handle ());
}
