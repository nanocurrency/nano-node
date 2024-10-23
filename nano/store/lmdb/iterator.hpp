#pragma once

#include <iterator>
#include <utility>
#include <variant>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace nano::store::lmdb
{
/**
 * @class iterator
 * @brief An LMDB database iterator.
 *
 * This class represents an iterator for LMDB (Lightning Memory-Mapped Database) databases.
 * It is a circular iterator, meaning that the end() sentinel value is always in the iteration cycle.
 *
 * Key characteristics:
 * - Decrementing the end iterator points to the last key in the database.
 * - Incrementing the end iterator points to the first key in the database.
 */
class iterator
{
	MDB_cursor * cursor{ nullptr };
	std::variant<std::monostate, std::pair<MDB_val, MDB_val>> current;
	void update (int status);
	iterator (MDB_txn * tx, MDB_dbi dbi) noexcept;

public:
	using iterator_category = std::bidirectional_iterator_tag;
	using value_type = std::pair<MDB_val, MDB_val>;
	using pointer = value_type *;
	using const_pointer = value_type const *;
	using reference = value_type &;
	using const_reference = value_type const &;

	static auto begin (MDB_txn * tx, MDB_dbi dbi) -> iterator;
	static auto end (MDB_txn * tx, MDB_dbi dbi) -> iterator;
	static auto lower_bound (MDB_txn * tx, MDB_dbi dbi, MDB_val const & lower_bound) -> iterator;

	~iterator ();

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
