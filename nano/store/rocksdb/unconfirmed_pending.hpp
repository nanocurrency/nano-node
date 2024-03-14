#pragma once

#include <nano/secure/pending_info.hpp>

#include <rocksdb/db.h>

namespace nano::store::rocksdb
{
class unconfirmed_pending
{
public:
	explicit unconfirmed_pending (::rocksdb::DB & db);
	auto del (store::unconfirmed_write_transaction const & tx, nano::pending_key const & key) -> void;
	auto exists (store::unconfirmed_transaction const & tx, nano::pending_key const & key) -> bool;
	auto get (store::unconfirmed_transaction const & tx, nano::pending_key const & key) -> std::optional<nano::pending_info>;
	auto lower_bound (store::unconfirmed_transaction const & tx, nano::account const & account, nano::block_hash const & hash) const -> std::optional<std::pair<nano::pending_key, nano::pending_info>>;
	auto put (store::unconfirmed_write_transaction const & tx, nano::pending_key const & key, nano::pending_info const & value) -> void;

private:
	/**
	 * Maps (destination account, pending block) to (source account, amount, version). (Removed)
	 * nano::account, nano::block_hash -> nano::account, nano::amount, nano::epoch
	 */
	std::unique_ptr<::rocksdb::ColumnFamilyHandle> handle;
};
} // namespace nano::store::rocksdb
