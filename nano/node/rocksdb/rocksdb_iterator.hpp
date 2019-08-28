#pragma once

#include <nano/secure/blockstore.hpp>

#include <rocksdb/db.h>
#include <rocksdb/filter_policy.h>
#include <rocksdb/options.h>
#include <rocksdb/slice.h>
#include <rocksdb/utilities/optimistic_transaction_db.h>
#include <rocksdb/utilities/transaction.h>

namespace
{
inline bool is_read (nano::transaction const & transaction_a)
{
	return (dynamic_cast<const nano::read_transaction *> (&transaction_a) != nullptr);
}

inline rocksdb::ReadOptions const & snapshot_options (nano::transaction const & transaction_a)
{
	assert (is_read (transaction_a));
	return *static_cast<const rocksdb::ReadOptions *> (transaction_a.get_handle ());
}
}

namespace nano
{
using rocksdb_val = db_val<rocksdb::Slice>;

template <typename T, typename U>
class rocksdb_iterator : public store_iterator_impl<T, U>
{
public:
	rocksdb_iterator (rocksdb::DB * db, nano::transaction const & transaction_a, rocksdb::ColumnFamilyHandle * handle_a, nano::epoch epoch_a = nano::epoch::unspecified) :
	cursor (nullptr)
	{
		current.first.epoch = epoch_a;
		current.second.epoch = epoch_a;

		rocksdb::Iterator * iter;
		if (is_read (transaction_a))
		{
			iter = db->NewIterator (snapshot_options (transaction_a), handle_a);
		}
		else
		{
			rocksdb::ReadOptions ropts;
			ropts.fill_cache = false;
			iter = tx (transaction_a)->GetIterator (ropts, handle_a);
		}

		cursor.reset (iter);
		cursor->SeekToFirst ();

		if (cursor->Valid ())
		{
			current.first.value = cursor->key ();
			current.second.value = cursor->value ();
		}
		else
		{
			clear ();
		}
	}

	rocksdb_iterator (std::nullptr_t, nano::epoch epoch_a = nano::epoch::unspecified) :
	cursor (nullptr)
	{
		current.first.epoch = epoch_a;
		current.second.epoch = epoch_a;
	}

	rocksdb_iterator (rocksdb::DB * db, nano::transaction const & transaction_a, rocksdb::ColumnFamilyHandle * handle_a, rocksdb_val const & val_a, nano::epoch epoch_a = nano::epoch::unspecified) :
	cursor (nullptr)
	{
		current.first.epoch = epoch_a;
		current.second.epoch = epoch_a;

		rocksdb::Iterator * iter;
		if (is_read (transaction_a))
		{
			iter = db->NewIterator (snapshot_options (transaction_a), handle_a);
		}
		else
		{
			iter = tx (transaction_a)->GetIterator (rocksdb::ReadOptions (), handle_a);
		}

		cursor.reset (iter);
		cursor->Seek (val_a);

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

	rocksdb_iterator (nano::rocksdb_iterator<T, U> && other_a)
	{
		cursor = other_a.cursor;
		other_a.cursor = nullptr;
		current = other_a.current;
	}

	rocksdb_iterator (nano::rocksdb_iterator<T, U> const &) = delete;

	nano::store_iterator_impl<T, U> & operator++ () override
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

	std::pair<nano::rocksdb_val, nano::rocksdb_val> * operator-> ()
	{
		return &current;
	}

	bool operator== (nano::store_iterator_impl<T, U> const & base_a) const override
	{
		auto const other_a (boost::polymorphic_downcast<nano::rocksdb_iterator<T, U> const *> (&base_a));

		if (!current.first.data () && !other_a->current.first.data ())
		{
			return true;
		}
		else if (!current.first.data () || !other_a->current.first.data ())
		{
			return false;
		}

		auto result (std::memcmp (current.first.data (), other_a->current.first.data (), current.first.size ()) == 0);
		assert (!result || (current.first.size () == other_a->current.first.size ()));
		assert (!result || (current.second.data () == other_a->current.second.data ()));
		assert (!result || (current.second.size () == other_a->current.second.size ()));
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
		current.first = nano::rocksdb_val (current.first.epoch);
		current.second = nano::rocksdb_val (current.second.epoch);
		assert (is_end_sentinal ());
	}
	nano::rocksdb_iterator<T, U> & operator= (nano::rocksdb_iterator<T, U> && other_a)
	{
		cursor = std::move (other_a.cursor);
		current = other_a.current;
		return *this;
	}
	nano::store_iterator_impl<T, U> & operator= (nano::store_iterator_impl<T, U> const &) = delete;

	std::unique_ptr<rocksdb::Iterator> cursor;
	std::pair<nano::rocksdb_val, nano::rocksdb_val> current;

private:
	rocksdb::Transaction * tx (nano::transaction const & transaction_a) const
	{
		return static_cast<rocksdb::Transaction *> (transaction_a.get_handle ());
	}
};

/**
 * Iterates the key/value pairs of two stores merged together
 */

template <typename T, typename U>
class rocksdb_merge_iterator : public store_iterator_impl<T, U>
{
public:
	rocksdb_merge_iterator (rocksdb::DB * db, nano::transaction const & transaction_a, rocksdb::ColumnFamilyHandle * db1_a, rocksdb::ColumnFamilyHandle * db2_a) :
	impl1 (std::make_unique<nano::rocksdb_iterator<T, U>> (db, transaction_a, db1_a, nano::epoch::epoch_0)),
	impl2 (std::make_unique<nano::rocksdb_iterator<T, U>> (db, transaction_a, db2_a, nano::epoch::epoch_1))
	{
	}
	rocksdb_merge_iterator (std::nullptr_t) :
	impl1 (std::make_unique<nano::rocksdb_iterator<T, U>> (nullptr, nano::epoch::epoch_0)),
	impl2 (std::make_unique<nano::rocksdb_iterator<T, U>> (nullptr, nano::epoch::epoch_1))
	{
	}

	rocksdb_merge_iterator (rocksdb::DB * db, nano::transaction const & transaction_a, rocksdb::ColumnFamilyHandle * db1_a, rocksdb::ColumnFamilyHandle * db2_a, rocksdb::Slice const & val_a) :
	impl1 (std::make_unique<nano::rocksdb_iterator<T, U>> (db, transaction_a, db1_a, val_a, nano::epoch::epoch_0)),
	impl2 (std::make_unique<nano::rocksdb_iterator<T, U>> (db, transaction_a, db2_a, val_a, nano::epoch::epoch_1))
	{
	}
	rocksdb_merge_iterator (nano::rocksdb_merge_iterator<T, U> && other_a)
	{
		impl1 = std::move (other_a.impl1);
		impl2 = std::move (other_a.impl2);
	}

	rocksdb_merge_iterator (nano::rocksdb_merge_iterator<T, U> const &) = delete;
	nano::store_iterator_impl<T, U> & operator++ () override
	{
		++least_iterator ();
		return *this;
	}
	std::pair<nano::rocksdb_val, nano::rocksdb_val> * operator-> ();
	bool operator== (nano::store_iterator_impl<T, U> const & base_a) const override
	{
		assert ((dynamic_cast<nano::rocksdb_merge_iterator<T, U> const *> (&base_a) != nullptr) && "Incompatible iterator comparison");
		auto & other (static_cast<nano::rocksdb_merge_iterator<T, U> const &> (base_a));
		return *impl1 == *other.impl1 && *impl2 == *other.impl2;
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
	nano::rocksdb_merge_iterator<T, U> & operator= (nano::rocksdb_merge_iterator<T, U> &&) = default;
	nano::rocksdb_merge_iterator<T, U> & operator= (nano::rocksdb_merge_iterator<T, U> const &) = delete;

private:
	nano::rocksdb_iterator<T, U> & least_iterator () const
	{
		nano::rocksdb_iterator<T, U> * result;
		if (impl1->is_end_sentinal ())
		{
			result = impl2.get ();
		}
		else if (impl2->is_end_sentinal ())
		{
			result = impl1.get ();
		}
		else
		{
			if (impl1->current.first.value.compare (impl2->current.first.value) < 0)
			{
				result = impl1.get ();
			}
			else if (impl1->current.first.value.compare (impl2->current.first.value) > 0)
			{
				result = impl2.get ();
			}
			else
			{
				result = impl1->current.second.value.compare (impl2->current.second.value) < 0 ? impl1.get () : impl2.get ();
			}
		}
		return *result;
	}

	std::unique_ptr<nano::rocksdb_iterator<T, U>> impl1;
	std::unique_ptr<nano::rocksdb_iterator<T, U>> impl2;
};
}
