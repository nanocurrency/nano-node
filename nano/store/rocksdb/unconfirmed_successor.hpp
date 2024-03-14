#pragma once

#include <nano/lib/numbers.hpp>

#include <rocksdb/db.h>

namespace nano::store::rocksdb
{
class unconfirmed_successor
{
public:
	explicit unconfirmed_successor (::rocksdb::DB & db);
	void del (store::unconfirmed_write_transaction const & tx, nano::block_hash const & key);
	bool exists (store::unconfirmed_transaction const & tx, nano::block_hash const & key) const;
	std::optional<nano::block_hash> get (store::unconfirmed_transaction const & tx, nano::block_hash const & key) const;
	void put (store::unconfirmed_write_transaction const & tx, nano::block_hash const & key, nano::block_hash const & value);

private:
	std::unique_ptr<::rocksdb::ColumnFamilyHandle> handle;
};
} // namespace nano::store::rocksdb
