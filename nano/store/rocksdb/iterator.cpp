#include "rocksdb/iterator.h"

#include <nano/lib/utility.hpp>
#include <nano/store/rocksdb/iterator.hpp>

namespace nano::store::rocksdb
{
auto iterator::is_end () const -> bool
{
	return std::holds_alternative<std::monostate> (current);
}

void iterator::update ()
{
	if (iter->Valid ())
	{
		current = std::make_pair (iter->key (), iter->value ());
	}
	else
	{
		current = std::monostate{};
	}
}

iterator::iterator (decltype (iter) && iter) :
	iter{ std::move (iter) }
{
	update ();
}

auto iterator::begin (::rocksdb::DB * db, std::variant<::rocksdb::Transaction *, ::rocksdb::ReadOptions *> snapshot, ::rocksdb::ColumnFamilyHandle * table) -> iterator
{
	auto result = iterator{ make_iterator (db, snapshot, table) };
	++result;
	return result;
}

auto iterator::end (::rocksdb::DB * db, std::variant<::rocksdb::Transaction *, ::rocksdb::ReadOptions *> snapshot, ::rocksdb::ColumnFamilyHandle * table) -> iterator
{
	return iterator{ make_iterator (db, snapshot, table) };
}

auto iterator::lower_bound (::rocksdb::DB * db, std::variant<::rocksdb::Transaction *, ::rocksdb::ReadOptions *> snapshot, ::rocksdb::ColumnFamilyHandle * table, ::rocksdb::Slice const & lower_bound) -> iterator
{
	auto iter = make_iterator (db, snapshot, table);
	iter->Seek (lower_bound);
	return iterator{ std::move (iter) };
}

auto iterator::make_iterator (::rocksdb::DB * db, std::variant<::rocksdb::Transaction *, ::rocksdb::ReadOptions *> snapshot, ::rocksdb::ColumnFamilyHandle * table) -> std::unique_ptr<::rocksdb::Iterator>
{
	return std::unique_ptr<::rocksdb::Iterator>{ std::visit ([&] (auto && ptr) {
		using V = std::remove_cvref_t<decltype (ptr)>;
		if constexpr (std::is_same_v<V, ::rocksdb::Transaction *>)
		{
			::rocksdb::ReadOptions ropts;
			ropts.fill_cache = false;
			return ptr->GetIterator (ropts, table);
		}
		else if constexpr (std::is_same_v<V, ::rocksdb::ReadOptions *>)
		{
			ptr->fill_cache = false;
			return db->NewIterator (*ptr, table);
		}
		else
		{
			static_assert (sizeof (V) == 0, "Missing variant handler for type V");
		}
	},
	snapshot) };
}

iterator::iterator (iterator && other) noexcept
{
	*this = std::move (other);
}

auto iterator::operator= (iterator && other) noexcept -> iterator &
{
	iter = std::move (other.iter);
	current = other.current;
	other.current = std::monostate{};
	return *this;
}

auto iterator::operator++ () -> iterator &
{
	if (!is_end ())
	{
		iter->Next ();
	}
	else
	{
		iter->SeekToFirst ();
	}
	update ();
	return *this;
}

auto iterator::operator-- () -> iterator &
{
	if (!is_end ())
	{
		iter->Prev ();
	}
	else
	{
		iter->SeekToLast ();
	}
	update ();
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
	return std::get<value_type> (current) == std::get<value_type> (other.current);
}
} // namespace nano::store::lmdb
