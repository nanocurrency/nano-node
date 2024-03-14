#pragma once

#include <nano/lib/blocks.hpp>
#include <nano/lib/numbers.hpp>

#include <rocksdb/db.h>

namespace nano::store
{
class transaction;
class write_transaction;
}

namespace nano::store::rocksdb
{
class unconfirmed_block
{
public:
	explicit unconfirmed_block (::rocksdb::DB & db);
	auto count (store::unconfirmed_transaction const & tx) -> uint64_t;
	auto del (store::unconfirmed_write_transaction const & tx, nano::block_hash const & key) -> void;
	auto exists (store::unconfirmed_transaction const & tx, nano::block_hash const & key) -> bool;
	auto put (store::unconfirmed_write_transaction const & tx, nano::block_hash const & key, nano::block const & value) -> void;
	auto get (store::unconfirmed_transaction const & tx, nano::block_hash const & key) const -> std::shared_ptr<nano::block>;

private:
	/**
	 * Contains block_sideband and block for all block types (legacy send/change/open/receive & state blocks)
	 * nano::block_hash -> nano::block_sideband, nano::block
	 */
	std::unique_ptr<::rocksdb::ColumnFamilyHandle> handle;
};
} // namespace nano::store::rocksdb
