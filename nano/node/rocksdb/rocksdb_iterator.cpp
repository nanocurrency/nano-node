#include <nano/lib/numbers.hpp>
#include <nano/node/rocksdb/rocksdb_iterator.hpp>

namespace
{
bool is_read (nano::transaction const & transaction_a)
{
	return (dynamic_cast<const nano::read_transaction *> (&transaction_a) != nullptr);
}

rocksdb::ReadOptions & snapshot_options (nano::transaction const & transaction_a)
{
	assert (is_read (transaction_a));
	return *static_cast<rocksdb::ReadOptions *> (transaction_a.get_handle ());
}
}

template <typename T, typename U>
nano::rocksdb_iterator<T, U>::rocksdb_iterator (rocksdb::DB * db, nano::transaction const & transaction_a, rocksdb::ColumnFamilyHandle * handle_a, nano::epoch epoch_a) :
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

template <typename T, typename U>
nano::rocksdb_iterator<T, U>::rocksdb_iterator (std::nullptr_t, nano::epoch epoch_a) :
cursor (nullptr)
{
	current.first.epoch = epoch_a;
	current.second.epoch = epoch_a;
}

template <typename T, typename U>
nano::rocksdb_iterator<T, U>::rocksdb_iterator (rocksdb::DB * db, nano::transaction const & transaction_a, rocksdb::ColumnFamilyHandle * handle_a, rocksdb_val const & val_a, nano::epoch epoch_a) :
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

template <typename T, typename U>
nano::rocksdb_iterator<T, U>::rocksdb_iterator (nano::rocksdb_iterator<T, U> && other_a)
{
	cursor = other_a.cursor;
	other_a.cursor = nullptr;
	current = other_a.current;
}

template <typename T, typename U>
nano::store_iterator_impl<T, U> & nano::rocksdb_iterator<T, U>::operator++ ()
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

template <typename T, typename U>
nano::rocksdb_iterator<T, U> & nano::rocksdb_iterator<T, U>::operator= (nano::rocksdb_iterator<T, U> && other_a)
{
	cursor = std::move (other_a.cursor);
	current = other_a.current;
	return *this;
}

template <typename T, typename U>
std::pair<nano::rocksdb_val, nano::rocksdb_val> * nano::rocksdb_iterator<T, U>::operator-> ()
{
	return &current;
}

template <typename T, typename U>
bool nano::rocksdb_iterator<T, U>::operator== (nano::store_iterator_impl<T, U> const & base_a) const
{
	auto const other_a (boost::polymorphic_downcast<nano::rocksdb_iterator<T, U> const *> (&base_a));
	auto result (current.first.data () == other_a->current.first.data ());
	assert (!result || (current.first.size () == other_a->current.first.size ()));
	assert (!result || (current.second.data () == other_a->current.second.data ()));
	assert (!result || (current.second.size () == other_a->current.second.size ()));
	return result;
}

template <typename T, typename U>
void nano::rocksdb_iterator<T, U>::clear ()
{
	current.first = nano::rocksdb_val (current.first.epoch);
	current.second = nano::rocksdb_val (current.second.epoch);
	assert (is_end_sentinal ());
}

template <typename T, typename U>
rocksdb::Transaction * nano::rocksdb_iterator<T, U>::tx (nano::transaction const & transaction_a) const
{
	return static_cast<rocksdb::Transaction *> (transaction_a.get_handle ());
}

template <typename T, typename U>
bool nano::rocksdb_iterator<T, U>::is_end_sentinal () const
{
	return current.first.size () == 0;
}

template <typename T, typename U>
void nano::rocksdb_iterator<T, U>::fill (std::pair<T, U> & value_a) const
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

template <typename T, typename U>
std::pair<nano::rocksdb_val, nano::rocksdb_val> * nano::rocksdb_merge_iterator<T, U>::operator-> ()
{
	return least_iterator ().operator-> ();
}

template <typename T, typename U>
nano::rocksdb_merge_iterator<T, U>::rocksdb_merge_iterator (rocksdb::DB * db, nano::transaction const & transaction_a, rocksdb::ColumnFamilyHandle * db1_a, rocksdb::ColumnFamilyHandle * db2_a) :
impl1 (std::make_unique<nano::rocksdb_iterator<T, U>> (db, transaction_a, db1_a, nano::epoch::epoch_0)),
impl2 (std::make_unique<nano::rocksdb_iterator<T, U>> (db, transaction_a, db2_a, nano::epoch::epoch_1))
{
}

template <typename T, typename U>
nano::rocksdb_merge_iterator<T, U>::rocksdb_merge_iterator (std::nullptr_t) :
impl1 (std::make_unique<nano::rocksdb_iterator<T, U>> (nullptr, nano::epoch::epoch_0)),
impl2 (std::make_unique<nano::rocksdb_iterator<T, U>> (nullptr, nano::epoch::epoch_1))
{
}

template <typename T, typename U>
nano::rocksdb_merge_iterator<T, U>::rocksdb_merge_iterator (rocksdb::DB * db, nano::transaction const & transaction_a, rocksdb::ColumnFamilyHandle * db1_a, rocksdb::ColumnFamilyHandle * db2_a, rocksdb::Slice const & val_a) :
impl1 (std::make_unique<nano::rocksdb_iterator<T, U>> (db, transaction_a, db1_a, val_a, nano::epoch::epoch_0)),
impl2 (std::make_unique<nano::rocksdb_iterator<T, U>> (db, transaction_a, db2_a, val_a, nano::epoch::epoch_1))
{
}

template <typename T, typename U>
nano::rocksdb_merge_iterator<T, U>::rocksdb_merge_iterator (nano::rocksdb_merge_iterator<T, U> && other_a)
{
	impl1 = std::move (other_a.impl1);
	impl2 = std::move (other_a.impl2);
}

template <typename T, typename U>
nano::store_iterator_impl<T, U> & nano::rocksdb_merge_iterator<T, U>::operator++ ()
{
	++least_iterator ();
	return *this;
}

template <typename T, typename U>
bool nano::rocksdb_merge_iterator<T, U>::is_end_sentinal () const
{
	return least_iterator ().is_end_sentinal ();
}

template <typename T, typename U>
void nano::rocksdb_merge_iterator<T, U>::fill (std::pair<T, U> & value_a) const
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

template <typename T, typename U>
bool nano::rocksdb_merge_iterator<T, U>::operator== (nano::store_iterator_impl<T, U> const & base_a) const
{
	assert ((dynamic_cast<nano::rocksdb_merge_iterator<T, U> const *> (&base_a) != nullptr) && "Incompatible iterator comparison");
	auto & other (static_cast<nano::rocksdb_merge_iterator<T, U> const &> (base_a));
	return *impl1 == *other.impl1 && *impl2 == *other.impl2;
}

template <typename T, typename U>
nano::rocksdb_iterator<T, U> & nano::rocksdb_merge_iterator<T, U>::least_iterator () const
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
