#include <nano/lib/utility.hpp>
#include <nano/store/lmdb/iterator.hpp>

namespace nano::store::lmdb
{
auto iterator::is_end () const -> bool
{
	return std::holds_alternative<std::monostate> (current);
}

void iterator::update (int status)
{
	if (status == MDB_SUCCESS)
	{
		value_type init;
		auto status = mdb_cursor_get (cursor, &init.first, &init.second, MDB_GET_CURRENT);
		release_assert (status == MDB_SUCCESS);
		current = init;
	}
	else
	{
		current = std::monostate{};
	}
}

iterator::iterator (MDB_txn * tx, MDB_dbi dbi) noexcept
{
	auto open_status = mdb_cursor_open (tx, dbi, &cursor);
	release_assert (open_status == MDB_SUCCESS);
	this->current = std::monostate{};
}

auto iterator::begin (MDB_txn * tx, MDB_dbi dbi) -> iterator
{
	iterator result{ tx, dbi };
	++result;
	return result;
}

auto iterator::end (MDB_txn * tx, MDB_dbi dbi) -> iterator
{
	return iterator{ tx, dbi };
}

auto iterator::lower_bound (MDB_txn * tx, MDB_dbi dbi, MDB_val const & lower_bound) -> iterator
{
	iterator result{ tx, dbi };
	auto status = mdb_cursor_get (result.cursor, const_cast<MDB_val *> (&lower_bound), nullptr, MDB_SET_RANGE);
	result.update (status);
	return std::move (result);
}

iterator::iterator (iterator && other) noexcept
{
	*this = std::move (other);
}

iterator::~iterator ()
{
	if (cursor)
	{
		mdb_cursor_close (cursor);
	}
}

auto iterator::operator= (iterator && other) noexcept -> iterator &
{
	cursor = other.cursor;
	other.cursor = nullptr;
	current = other.current;
	other.current = std::monostate{};
	return *this;
}

auto iterator::operator++ () -> iterator &
{
	auto operation = is_end () ? MDB_FIRST : MDB_NEXT;
	auto status = mdb_cursor_get (cursor, nullptr, nullptr, operation);
	release_assert (status == MDB_SUCCESS || status == MDB_NOTFOUND);
	update (status);
	return *this;
}

auto iterator::operator-- () -> iterator &
{
	auto operation = is_end () ? MDB_LAST : MDB_PREV;
	auto status = mdb_cursor_get (cursor, nullptr, nullptr, operation);
	release_assert (status == MDB_SUCCESS || status == MDB_NOTFOUND);
	update (status);
	return *this;
}

auto iterator::operator->() const -> const_pointer
{
	release_assert (!is_end ());
	return std::get_if<value_type> (&current);
}

auto iterator::operator* () const -> const_reference
{
	release_assert (!is_end ());
	return std::get<value_type> (current);
}

auto iterator::operator== (iterator const & other) const -> bool
{
	if (is_end () != other.is_end ())
	{
		return false;
	}
	if (is_end ())
	{
		return true;
	}
	auto & lhs = std::get<value_type> (current);
	auto & rhs = std::get<value_type> (other.current);
	auto result = lhs.first.mv_data == rhs.first.mv_data;
	if (!result)
	{
		return result;
	}
	debug_assert (std::make_pair (lhs.first.mv_data, lhs.first.mv_size) == std::make_pair (rhs.first.mv_data, rhs.first.mv_size) && std::make_pair (lhs.second.mv_data, lhs.second.mv_size) == std::make_pair (rhs.second.mv_data, rhs.second.mv_size));
	return result;
}
} // namespace nano::store::lmdb
