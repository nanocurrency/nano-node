#pragma once

#include <nano/secure/account_info.hpp>

#include <rocksdb/db.h>

namespace nano::store
{
class unconfirmed_transaction;
class unconfirmed_write_transaction;
}
namespace nano::store::rocksdb
{
class component;
}

namespace nano::store::rocksdb
{
class unconfirmed_account
{
public:
	explicit unconfirmed_account (::rocksdb::DB & db);
	void del (store::unconfirmed_write_transaction const & tx, nano::account const & key);
	bool exists (store::unconfirmed_transaction const & tx, nano::account const & key) const;
	std::optional<nano::account_info> get (store::unconfirmed_transaction const & tx, nano::account const & key) const;
	void put (store::unconfirmed_write_transaction const & tx, nano::account const & key, nano::account_info const & value);

private:
	/**
	 * Maps account v0 to account information, head, rep, open, balance, timestamp, block count and epoch
	 * nano::account -> nano::block_hash, nano::block_hash, nano::block_hash, nano::amount, uint64_t, uint64_t, nano::epoch
	 */
	std::unique_ptr<::rocksdb::ColumnFamilyHandle> handle;
};
} // amespace nano::store::rocksdb
