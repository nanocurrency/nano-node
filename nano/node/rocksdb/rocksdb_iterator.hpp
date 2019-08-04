#pragma once

#include <rocksdb/db.h>
#include <rocksdb/filter_policy.h>
#include <rocksdb/options.h>
#include <rocksdb/slice.h>
#include <rocksdb/utilities/optimistic_transaction_db.h>
#include <rocksdb/utilities/transaction.h>

#include <nano/secure/blockstore.hpp>

namespace nano
{
using rocksdb_val = db_val<rocksdb::Slice>;

template <typename T, typename U>
class rocksdb_iterator : public store_iterator_impl<T, U>
{
public:
	rocksdb_iterator (rocksdb::DB * db, nano::transaction const & transaction_a, rocksdb::ColumnFamilyHandle * handle, nano::epoch = nano::epoch::unspecified);
	rocksdb_iterator (std::nullptr_t, nano::epoch = nano::epoch::unspecified);
	rocksdb_iterator (rocksdb::DB * db, nano::transaction const & transaction_a, rocksdb::ColumnFamilyHandle * handle, nano::rocksdb_val const & val_a, nano::epoch = nano::epoch::unspecified);
	rocksdb_iterator (nano::rocksdb_iterator<T, U> && other_a);
	rocksdb_iterator (nano::rocksdb_iterator<T, U> const &) = delete;
	nano::store_iterator_impl<T, U> & operator++ () override;
	std::pair<nano::rocksdb_val, nano::rocksdb_val> * operator-> ();
	bool operator== (nano::store_iterator_impl<T, U> const & other_a) const override;
	bool is_end_sentinal () const override;
	void fill (std::pair<T, U> &) const override;
	void clear ();
	nano::rocksdb_iterator<T, U> & operator= (nano::rocksdb_iterator<T, U> && other_a);
	nano::store_iterator_impl<T, U> & operator= (nano::store_iterator_impl<T, U> const &) = delete;

	std::unique_ptr<rocksdb::Iterator> cursor;
	std::pair<nano::rocksdb_val, nano::rocksdb_val> current;

private:
	rocksdb::Transaction * tx (nano::transaction const &) const;
};

/**
 * Iterates the key/value pairs of two stores merged together
 */

template <typename T, typename U>
class rocksdb_merge_iterator : public store_iterator_impl<T, U>
{
public:
	rocksdb_merge_iterator (rocksdb::DB * db, nano::transaction const &, rocksdb::ColumnFamilyHandle *, rocksdb::ColumnFamilyHandle *);
	rocksdb_merge_iterator (std::nullptr_t);
	rocksdb_merge_iterator (rocksdb::DB * db, nano::transaction const &, rocksdb::ColumnFamilyHandle *, rocksdb::ColumnFamilyHandle *, rocksdb::Slice const &);
	rocksdb_merge_iterator (nano::rocksdb_merge_iterator<T, U> &&);
	rocksdb_merge_iterator (nano::rocksdb_merge_iterator<T, U> const &) = delete;
	nano::store_iterator_impl<T, U> & operator++ () override;
	std::pair<nano::rocksdb_val, nano::rocksdb_val> * operator-> ();
	bool operator== (nano::store_iterator_impl<T, U> const &) const override;
	bool is_end_sentinal () const override;
	void fill (std::pair<T, U> &) const override;
	nano::rocksdb_merge_iterator<T, U> & operator= (nano::rocksdb_merge_iterator<T, U> &&) = default;
	nano::rocksdb_merge_iterator<T, U> & operator= (nano::rocksdb_merge_iterator<T, U> const &) = delete;

private:
	nano::rocksdb_iterator<T, U> & least_iterator () const;
	std::unique_ptr<nano::rocksdb_iterator<T, U>> impl1;
	std::unique_ptr<nano::rocksdb_iterator<T, U>> impl2;
};
}
