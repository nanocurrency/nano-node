#pragma once

#include <nano/secure/pending_info.hpp>

#include <rocksdb/db.h>

namespace nano::store
{
class unconfirmed_transaction;
class unconfirmed_write_transaction;
}

namespace nano::store::rocksdb
{
class unconfirmed_received
{
public:
	explicit unconfirmed_received (::rocksdb::DB & db);
	void del (store::unconfirmed_write_transaction const & tx, nano::pending_key const & key);
	bool exists (store::unconfirmed_transaction const & tx, nano::pending_key const & key) const;
	void put (store::unconfirmed_write_transaction const & tx, nano::pending_key const & key);

private:
	std::unique_ptr<::rocksdb::ColumnFamilyHandle> handle;
};
} // namespace nano::store::rocksdb
