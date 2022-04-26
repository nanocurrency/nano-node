#pragma once

#include <nano/node/lmdb/lmdb_iterator.hpp>

namespace nano
{
class mdb_block_iterator : public store_iterator_impl<nano::block_hash, nano::block_w_sideband>
{
private:
	using Base = nano::store_iterator_impl<nano::block_hash, nano::block_w_sideband>;

public:
	mdb_block_iterator () = default;

	explicit mdb_block_iterator (nano::transaction const & transaction_a, MDB_dbi indexes_db_a, MDB_dbi contents_db_a, MDB_val const & val_a = MDB_val{}, bool const direction_asc = true)
	{
		auto status (mdb_cursor_open (static_cast<MDB_txn *> (transaction_a.get_handle ()), indexes_db_a, &index_cursor));
		release_assert (status == 0);
		auto operation (MDB_SET_RANGE);
		if (val_a.mv_size != 0)
		{
			current.index = val_a;
		}
		else
		{
			operation = direction_asc ? MDB_FIRST : MDB_LAST;
		}

		auto status2 (mdb_cursor_get (index_cursor, &current.index.value, &current.contents.hash_and_block.first.value, operation));
		current.contents.is_updated = false;
		release_assert (status2 == 0 || status2 == MDB_NOTFOUND);
		if (status2 != MDB_NOTFOUND)
		{
			auto status3 (mdb_cursor_get (index_cursor, &current.index.value, &current.contents.hash_and_block.first.value, MDB_GET_CURRENT));
			release_assert (status3 == 0 || status3 == MDB_NOTFOUND);
			if (current.index.size () != sizeof (std::uint64_t))
			{
				clear ();
			}
		}
		else
		{
			clear ();
		}
	}

	mdb_block_iterator (nano::mdb_block_iterator && other_a) noexcept
	{
		index_cursor = other_a.index_cursor;
		other_a.index_cursor = nullptr;

		current = std::move (other_a.current);
	}

	mdb_block_iterator (nano::mdb_block_iterator const &) = delete;

	~mdb_block_iterator () override
	{
		if (index_cursor != nullptr)
		{
			mdb_cursor_close (index_cursor);
		}
	}

	Base & operator++ () override
	{
		debug_assert (index_cursor != nullptr);
		auto status (mdb_cursor_get (index_cursor, &current.index.value, &current.contents.hash_and_block.first.value, MDB_NEXT));
		current.contents.is_updated = false;
		release_assert (status == 0 || status == MDB_NOTFOUND);
		if (status == MDB_NOTFOUND)
		{
			clear ();
		}

		if (current.index.size () != sizeof (std::uint64_t))
		{
			clear ();
		}

		return *this;
	}

	Base & operator-- () override
	{
		debug_assert (index_cursor != nullptr);
		auto status (mdb_cursor_get (index_cursor, &current.index.value, &current.contents.hash_and_block.first.value, MDB_PREV));
		current.contents.is_updated = false;
		release_assert (status == 0 || status == MDB_NOTFOUND);
		if (status == MDB_NOTFOUND)
		{
			clear ();
		}

		if (current.index.size () != sizeof (std::uint64_t))
		{
			clear ();
		}

		return *this;
	}

	std::pair<nano::db_val<MDB_val>, nano::db_val<MDB_val>> * operator-> ()
	{
		if (!current.contents.is_updated)
		{
			// TODO: update current.contents.hash_and_block.second by reading from block_contents at key current.contents.hash_and_block.first
			current.contents.is_updated = true;
		}

		return &current.contents.hash_and_block;
	}

	bool operator== (Base const & base_a) const override
	{
		auto const other_a (boost::polymorphic_downcast<mdb_block_iterator const *> (&base_a));
		auto result (current.index.size () == other_a->current.index.size ());
		debug_assert (!result || (current.index.data () == other_a->current.index.data ()));
		debug_assert (!result || (current.contents.hash_and_block.first.size () == other_a->current.contents.hash_and_block.first.size ()));
		debug_assert (!result || (current.contents.hash_and_block.first.data () == other_a->current.contents.hash_and_block.first.data ()));
		debug_assert (!result || (current.contents.hash_and_block.second.size () == other_a->current.contents.hash_and_block.second.size ()));
		debug_assert (!result || (current.contents.hash_and_block.second.data () == other_a->current.contents.hash_and_block.second.data ()));
		return result;
	}

	bool is_end_sentinel () const override
	{
		return current.index.size () == 0;
	}

	void fill (std::pair<nano::block_hash, nano::block_w_sideband> & value_a) const override
	{
		if (current.contents.hash_and_block.first.size () != 0)
		{
			value_a.first = static_cast<nano::block_hash> (current.contents.hash_and_block.first);
		}
		else
		{
			value_a.first = nano::block_hash{};
		}

		// TODO: if current.contents.is_updated is false, trigger update?
		if (current.contents.hash_and_block.second.size () != 0)
		{
			value_a.second = static_cast<nano::block_w_sideband> (current.contents.hash_and_block.second);
		}
		else
		{
			value_a.second = nano::block_w_sideband{};
		}
	}

	void clear ()
	{
		current = {};
		debug_assert (is_end_sentinel ());
	}

	nano::mdb_block_iterator & operator= (nano::mdb_block_iterator && other_a) noexcept
	{
		if (index_cursor != nullptr)
		{
			mdb_cursor_close (index_cursor);
		}

		index_cursor = other_a.index_cursor;
		other_a.index_cursor = nullptr;

		current = std::move (other_a.current);
		other_a.clear ();

		return *this;
	}

	Base & operator= (Base const &) = delete;

private:
	struct block_contents_entry
	{
		using hash = nano::db_val<MDB_val>;
		using block = nano::db_val<MDB_val>;
		std::pair<hash, block> hash_and_block;
		bool is_updated{ false };
	};

	struct entry
	{
		nano::db_val<MDB_val> index;
		block_contents_entry contents;
	};

	MDB_cursor * index_cursor{};
	entry current{};
};
}
