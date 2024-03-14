#include <nano/store/rocksdb/unconfirmed_transaction.hpp>

nano::store::unconfirmed_read_transaction::unconfirmed_read_transaction (std::unique_ptr<read_transaction_impl> && read_transaction_impl) :
	tx{ std::make_unique<read_transaction> (std::move (read_transaction_impl)) }
{
}

nano::store::unconfirmed_write_transaction::unconfirmed_write_transaction (std::unique_ptr<write_transaction_impl> && write_transaction_impl) :
	tx{ std::make_unique<write_transaction> (std::move (write_transaction_impl)) }
{

}
