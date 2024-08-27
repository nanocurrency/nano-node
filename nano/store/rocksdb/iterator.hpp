#pragma once

#include <nano/store/component.hpp>
#include <nano/store/iterator.hpp>
#include <nano/store/rocksdb/db_val.hpp>
#include <nano/store/transaction.hpp>

#include <rocksdb/db.h>
#include <rocksdb/filter_policy.h>
#include <rocksdb/options.h>
#include <rocksdb/slice.h>
#include <rocksdb/utilities/transaction.h>

namespace
{
inline bool is_read (nano::store::transaction const & transaction_a)
{
	return (dynamic_cast<nano::store::read_transaction const *> (&transaction_a) != nullptr);
}

inline rocksdb::ReadOptions & snapshot_options (nano::store::transaction const & transaction_a)
{
	debug_assert (is_read (transaction_a));
	return *static_cast<rocksdb::ReadOptions *> (transaction_a.get_handle ());
}
}

namespace nano::store::rocksdb
{
template <typename T, typename U>
class iterator : public iterator_impl<T, U>
{
public:
	iterator () = default;

	iterator (::rocksdb::DB * db, store::transaction const & transaction_a, ::rocksdb::ColumnFamilyHandle * handle_a, db_val const * val_a, bool const direction_asc) :
		nano::store::iterator_impl<T, U> (transaction_a)
	{
		// Don't fill the block cache for any blocks read as a result of an iterator
		if (is_read (transaction_a))
		{
			auto read_options = snapshot_options (transaction_a);
			read_options.fill_cache = false;
			cursor.reset (db->NewIterator (read_options, handle_a));
		}
		else
		{
			::rocksdb::ReadOptions ropts;
			ropts.fill_cache = false;
			cursor.reset (tx (transaction_a)->GetIterator (ropts, handle_a));
		}

		if (val_a)
		{
			cursor->Seek (*val_a);
		}
		else if (direction_asc)
		{
			cursor->SeekToFirst ();
		}
		else
		{
			cursor->SeekToLast ();
		}

		if (cursor->Valid ())
		{
			current.first = cursor->key ();
			current.second = cursor->value ();
		}
		else
		{
			clear ();
		}
	}

	iterator (::rocksdb::DB * db, store::transaction const & transaction_a, ::rocksdb::ColumnFamilyHandle * handle_a) :
		iterator (db, transaction_a, handle_a, nullptr)
	{
	}

	iterator (nano::store::rocksdb::iterator<T, U> && other_a)
	{
		cursor = other_a.cursor;
		other_a.cursor = nullptr;
		current = other_a.current;
	}

	iterator (nano::store::rocksdb::iterator<T, U> const &) = delete;

	store::iterator_impl<T, U> & operator++ () override
	{
		cursor->Next ();
		if (cursor->Valid ())
		{
			current.first = cursor->key ();
			current.second = cursor->value ();

			if (current.first.size () != sizeof (T))
			{
				clear ();
			}
		}
		else
		{
			clear ();
		}

		return *this;
	}

	store::iterator_impl<T, U> & operator-- () override
	{
		cursor->Prev ();
		if (cursor->Valid ())
		{
			current.first = cursor->key ();
			current.second = cursor->value ();

			if (current.first.size () != sizeof (T))
			{
				clear ();
			}
		}
		else
		{
			clear ();
		}

		return *this;
	}

	std::pair<nano::store::rocksdb::db_val, nano::store::rocksdb::db_val> * operator->()
	{
		return &current;
	}

	bool operator== (store::iterator_impl<T, U> const & base_a) const override
	{
		auto const other_a (boost::polymorphic_downcast<nano::store::rocksdb::iterator<T, U> const *> (&base_a));

		if (!current.first.data () && !other_a->current.first.data ())
		{
			return true;
		}
		else if (!current.first.data () || !other_a->current.first.data ())
		{
			return false;
		}

		auto result (std::memcmp (current.first.data (), other_a->current.first.data (), current.first.size ()) == 0);
		debug_assert (!result || (current.first.size () == other_a->current.first.size ()));
		debug_assert (!result || std::memcmp (current.second.data (), other_a->current.second.data (), current.second.size ()) == 0);
		debug_assert (!result || (current.second.size () == other_a->current.second.size ()));
		return result;
	}

	bool is_end_sentinal () const override
	{
		return current.first.size () == 0;
	}

	void fill (std::pair<T, U> & value_a) const override
	{
		{
			if (current.first.size () != 0)
			{
				value_a.first = static_cast<T> (current.first);
			}
			else
			{
				value_a.first = T ();
			}
			if (current.second.size () != 0)
			{
				value_a.second = static_cast<U> (current.second);
			}
			else
			{
				value_a.second = U ();
			}
		}
	}
	void clear ()
	{
		current.first = nano::store::rocksdb::db_val{};
		current.second = nano::store::rocksdb::db_val{};
		debug_assert (is_end_sentinal ());
	}
	nano::store::rocksdb::iterator<T, U> & operator= (nano::store::rocksdb::iterator<T, U> && other_a)
	{
		cursor = std::move (other_a.cursor);
		current = other_a.current;
		return *this;
	}
	store::iterator_impl<T, U> & operator= (store::iterator_impl<T, U> const &) = delete;

	std::unique_ptr<::rocksdb::Iterator> cursor;
	std::pair<nano::store::rocksdb::db_val, nano::store::rocksdb::db_val> current;

private:
	::rocksdb::Transaction * tx (store::transaction const & transaction_a) const
	{
		return static_cast<::rocksdb::Transaction *> (transaction_a.get_handle ());
	}
};
}
