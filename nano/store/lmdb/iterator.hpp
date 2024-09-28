#pragma once

#include <nano/store/component.hpp>
#include <nano/store/db_val.hpp>
#include <nano/store/iterator.hpp>
#include <nano/store/lmdb/utility.hpp>
#include <nano/store/transaction.hpp>

#include <lmdbxx/lmdb++.h>

namespace nano::store::lmdb
{
template <typename T, typename U>
class iterator : public iterator_impl<T, U>
{
public:
	iterator (store::transaction const & transaction_a, ::lmdb::env const & env_a, ::lmdb::dbi const & db_a, MDB_val const & val_a = MDB_val{}, bool const direction_asc = true) :
		nano::store::iterator_impl<T, U> (transaction_a),
		cursor{ ::lmdb::cursor::open (tx (transaction_a), db_a) }
	{
		auto operation = MDB_SET_RANGE;
		if (val_a.mv_size != 0)
		{
			current.first = val_a;
		}
		else
		{
			operation = direction_asc ? MDB_FIRST : MDB_LAST;
		}
		if (::lmdb::cursor_get (cursor, &current.first.value, &current.second.value, operation))
		{
			::lmdb::cursor_get (cursor, &current.first.value, &current.second.value, MDB_GET_CURRENT);
			if (current.first.size () != sizeof (T))
			{
				clear ();
			}
		}
		else
		{
			clear ();
		}
	}

	iterator () = default;

	iterator (nano::store::lmdb::iterator<T, U> && other_a)
	{
		cursor = other_a.cursor;
		other_a.cursor = nullptr;
		current = other_a.current;
	}

	iterator (nano::store::lmdb::iterator<T, U> const &) = delete;

	~iterator ()
	{
		if (cursor != nullptr)
		{
			cursor.close ();
		}
	}

	store::iterator_impl<T, U> & operator++ () override
	{
		debug_assert (cursor != nullptr);
		if (!::lmdb::cursor_get (cursor, &current.first.value, &current.second.value, MDB_NEXT))
		{
			clear ();
		}
		if (current.first.size () != sizeof (T))
		{
			clear ();
		}
		return *this;
	}

	store::iterator_impl<T, U> & operator-- () override
	{
		debug_assert (cursor != nullptr);
		if (!::lmdb::cursor_get (cursor, &current.first.value, &current.second.value, MDB_PREV))
		{
			clear ();
		}
		if (current.first.size () != sizeof (T))
		{
			clear ();
		}
		return *this;
	}

	std::pair<store::db_val<MDB_val>, store::db_val<MDB_val>> * operator->()
	{
		return &current;
	}

	bool operator== (nano::store::lmdb::iterator<T, U> const & base_a) const
	{
		auto const other_a (boost::polymorphic_downcast<nano::store::lmdb::iterator<T, U> const *> (&base_a));
		auto result (current.first.data () == other_a->current.first.data ());
		debug_assert (!result || (current.first.size () == other_a->current.first.size ()));
		debug_assert (!result || (current.second.data () == other_a->current.second.data ()));
		debug_assert (!result || (current.second.size () == other_a->current.second.size ()));
		return result;
	}

	bool operator== (store::iterator_impl<T, U> const & base_a) const override
	{
		auto const other_a (boost::polymorphic_downcast<nano::store::lmdb::iterator<T, U> const *> (&base_a));
		auto result (current.first.data () == other_a->current.first.data ());
		debug_assert (!result || (current.first.size () == other_a->current.first.size ()));
		debug_assert (!result || (current.second.data () == other_a->current.second.data ()));
		debug_assert (!result || (current.second.size () == other_a->current.second.size ()));
		return result;
	}

	bool is_end_sentinal () const override
	{
		return current.first.size () == 0;
	}
	void fill (std::pair<T, U> & value_a) const override
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
	void clear ()
	{
		current.first = store::db_val<MDB_val> ();
		current.second = store::db_val<MDB_val> ();
		debug_assert (is_end_sentinal ());
	}

	nano::store::lmdb::iterator<T, U> & operator= (nano::store::lmdb::iterator<T, U> && other_a)
	{
		if (cursor != nullptr)
		{
			cursor.close ();
		}
		cursor = other_a.cursor;
		other_a.cursor = nullptr;
		current = other_a.current;
		other_a.clear ();
		return *this;
	}

	store::iterator_impl<T, U> & operator= (store::iterator_impl<T, U> const &) = delete;
	::lmdb::cursor cursor;
	std::pair<store::db_val<MDB_val>, store::db_val<MDB_val>> current;
};

/**
 * Iterates the key/value pairs of two stores merged together
 */
template <typename T, typename U>
class merge_iterator : public iterator_impl<T, U>
{
public:
	merge_iterator (store::transaction const & transaction_a, ::lmdb::dbi const & db1_a, ::lmdb::dbi const & db2_a) :
		impl1 (std::make_unique<nano::store::lmdb::iterator<T, U>> (transaction_a, db1_a)),
		impl2 (std::make_unique<nano::store::lmdb::iterator<T, U>> (transaction_a, db2_a))
	{
	}

	merge_iterator () :
		impl1 (std::make_unique<nano::store::lmdb::iterator<T, U>> ()),
		impl2 (std::make_unique<nano::store::lmdb::iterator<T, U>> ())
	{
	}

	merge_iterator (store::transaction const & transaction_a, ::lmdb::dbi const & db1_a, ::lmdb::dbi const & db2_a, MDB_val const & val_a) :
		impl1 (std::make_unique<nano::store::lmdb::iterator<T, U>> (transaction_a, db1_a, val_a)),
		impl2 (std::make_unique<nano::store::lmdb::iterator<T, U>> (transaction_a, db2_a, val_a))
	{
	}

	merge_iterator (merge_iterator<T, U> && other_a)
	{
		impl1 = std::move (other_a.impl1);
		impl2 = std::move (other_a.impl2);
	}

	merge_iterator (merge_iterator<T, U> const &) = delete;

	store::iterator_impl<T, U> & operator++ () override
	{
		++least_iterator ();
		return *this;
	}

	store::iterator_impl<T, U> & operator-- () override
	{
		--least_iterator ();
		return *this;
	}

	std::pair<store::db_val<MDB_val>, store::db_val<MDB_val>> * operator->()
	{
		return least_iterator ().operator->();
	}

	bool operator== (merge_iterator<T, U> const & other) const
	{
		return *impl1 == *other.impl1 && *impl2 == *other.impl2;
	}

	bool operator!= (merge_iterator<T, U> const & base_a) const
	{
		return !(*this == base_a);
	}

	bool operator== (store::iterator_impl<T, U> const & base_a) const override
	{
		debug_assert ((dynamic_cast<merge_iterator<T, U> const *> (&base_a) != nullptr) && "Incompatible iterator comparison");
		auto & other (static_cast<merge_iterator<T, U> const &> (base_a));
		return *this == other;
	}

	bool is_end_sentinal () const override
	{
		return least_iterator ().is_end_sentinal ();
	}

	void fill (std::pair<T, U> & value_a) const override
	{
		auto & current (least_iterator ());
		if (current->first.size () != 0)
		{
			value_a.first = static_cast<T> (current->first);
		}
		else
		{
			value_a.first = T ();
		}
		if (current->second.size () != 0)
		{
			value_a.second = static_cast<U> (current->second);
		}
		else
		{
			value_a.second = U ();
		}
	}
	merge_iterator<T, U> & operator= (merge_iterator<T, U> &&) = default;
	merge_iterator<T, U> & operator= (merge_iterator<T, U> const &) = delete;

	mutable bool from_first_database{ false };

private:
	nano::store::lmdb::iterator<T, U> & least_iterator () const
	{
		nano::store::lmdb::iterator<T, U> * result;
		if (impl1->is_end_sentinal ())
		{
			result = impl2.get ();
			from_first_database = false;
		}
		else if (impl2->is_end_sentinal ())
		{
			result = impl1.get ();
			from_first_database = true;
		}
		else
		{
			auto key_cmp (mdb_cmp (impl1->cursor.txn (), impl1->cursor.dbi (), impl1->current.first, impl2->current.first));

			if (key_cmp < 0)
			{
				result = impl1.get ();
				from_first_database = true;
			}
			else if (key_cmp > 0)
			{
				result = impl2.get ();
				from_first_database = false;
			}
			else
			{
				auto val_cmp (mdb_cmp (impl1->cursor.txn (), impl1->cursor.dbi (), impl1->current.second, impl2->current.second));
				result = val_cmp < 0 ? impl1.get () : impl2.get ();
				from_first_database = (result == impl1.get ());
			}
		}
		return *result;
	}

	std::unique_ptr<nano::store::lmdb::iterator<T, U>> impl1;
	std::unique_ptr<nano::store::lmdb::iterator<T, U>> impl2;
};
}
