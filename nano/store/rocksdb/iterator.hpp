#pragma once

#include <iterator>
#include <memory>
#include <utility>
#include <variant>

#include <rocksdb/db.h>
#include <rocksdb/slice.h>
#include <rocksdb/utilities/transaction.h>

namespace nano::store::rocksdb
{
/**
 * @class iterator
 * @brief A RocksDB database iterator.
 *
 * This class represents an iterator for RocksDB (Persistent Key-Value Store) databases.
 * It is a circular iterator, meaning that the end() sentinel value is always in the iteration cycle.
 *
 * Key characteristics:
 * - Decrementing the end iterator points to the last key in the database.
 * - Incrementing the end iterator points to the first key in the database.
 */
class iterator
{
	std::unique_ptr<::rocksdb::Iterator> iter;
	std::variant<std::monostate, std::pair<::rocksdb::Slice, ::rocksdb::Slice>> current;
	void update ();
	iterator (decltype (iter) && iter);
	static auto make_iterator (::rocksdb::DB * db, std::variant<::rocksdb::Transaction *, ::rocksdb::ReadOptions *> snapshot, ::rocksdb::ColumnFamilyHandle * table) -> std::unique_ptr<::rocksdb::Iterator>;

public:
	using iterator_category = std::bidirectional_iterator_tag;
	using value_type = std::pair<::rocksdb::Slice, ::rocksdb::Slice>;
	using pointer = value_type *;
	using const_pointer = value_type const *;
	using reference = value_type &;
	using const_reference = value_type const &;

	static auto begin (::rocksdb::DB * db, std::variant<::rocksdb::Transaction *, ::rocksdb::ReadOptions *> snapshot, ::rocksdb::ColumnFamilyHandle * table) -> iterator;
	static auto end (::rocksdb::DB * db, std::variant<::rocksdb::Transaction *, ::rocksdb::ReadOptions *> snapshot, ::rocksdb::ColumnFamilyHandle * table) -> iterator;
	static auto lower_bound (::rocksdb::DB * db, std::variant<::rocksdb::Transaction *, ::rocksdb::ReadOptions *> snapshot, ::rocksdb::ColumnFamilyHandle * table, ::rocksdb::Slice const & lower_bound) -> iterator;

	iterator (iterator const &) = delete;
	auto operator= (iterator const &) -> iterator & = delete;

	iterator (iterator && other_a) noexcept;
	auto operator= (iterator && other) noexcept -> iterator &;

	auto operator++ () -> iterator &;
	auto operator-- () -> iterator &;
	auto operator->() const -> const_pointer;
	auto operator* () const -> const_reference;
	auto operator== (iterator const & other) const -> bool;
	auto operator!= (iterator const & other) const -> bool
	{
		return !(*this == other);
	}
	bool is_end () const;
};
}
