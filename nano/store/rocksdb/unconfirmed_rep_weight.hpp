#pragma once

#include <nano/lib/numbers.hpp>

#include <rocksdb/db.h>

namespace nano::store::rocksdb
{
class unconfirmed_rep_weight
{
public:
	explicit unconfirmed_rep_weight (::rocksdb::DB & db);
	auto del (store::unconfirmed_write_transaction const & tx, nano::account const & key) -> void;
	auto exists (store::unconfirmed_transaction const & tx, nano::account const & key) const -> bool;
	auto count (store::unconfirmed_transaction const & tx) -> uint64_t;
	auto get (store::unconfirmed_transaction const & tx, nano::account const & key) const -> std::optional<nano::uint128_t>;
	auto put (store::unconfirmed_write_transaction const & tx, nano::account const & key, nano::amount const & value) -> void;

private:
	std::unique_ptr<::rocksdb::ColumnFamilyHandle> handle;
};
} // namespace nano::store::rocksdb
