#pragma once

#include <nano/lib/config.hpp>
#include <nano/lib/lmdbconfig.hpp>
#include <nano/lib/logger_mt.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/node/dht/dht_iterator.hpp>
#include <nano/node/lmdb/lmdb.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/store/unchecked_store_partial.hpp>
#include <nano/secure/store_partial.hpp>

#include <boost/pointer_cast.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>

#include <diskhash.hpp>

namespace nano
{
constexpr size_t sizeof_unchecked_info = 258;
constexpr size_t sizeof_unchecked_key = 64;

template <typename DATA_TYPE, size_t DATA_SIZE>
struct DHT_val
{
	using data_type = DATA_TYPE;
	constexpr size_t data_size ()
	{
		return DATA_SIZE;
	}
	DHT_val () = default;
	DHT_val (DHT_val const &) = default;
	DHT_val (DATA_TYPE * mv_data_a)
	{
		release_assert (mv_data_a != nullptr);
		std::memcpy (mv_data, mv_data_a, data_size () * sizeof (data_type));
	}
	DHT_val (void * mv_data_a) :
		DHT_val ((data_type *)mv_data_a)
	{
	}
	DATA_TYPE mv_data[DATA_SIZE];
};
using unchecked_key_dht = DHT_val<uint8_t, sizeof_unchecked_key>;
using unchecked_info_dht = DHT_val<uint8_t, sizeof_unchecked_info>;
using unchecked_dht_val = db_val<unchecked_info_dht>;

class logging_mt;
class dht_config;
class dht_mdb_store;

class unchecked_dht_mdb_store : public unchecked_mdb_store
{
	friend class dht_mdb_store;

public:
	explicit unchecked_dht_mdb_store (
	nano::dht_mdb_store & dht_mdb_store_a,
	boost::filesystem::path const & dht_path_a);

	virtual void clear (nano::write_transaction const &) override
	{
		dht.clear ();
	}

	virtual void put (nano::write_transaction const & transaction_a, nano::unchecked_key const & unchecked_key_a, nano::unchecked_info const & value_a) override
	{
		std::string hex_key;
		unchecked_key_a.encode_hex (hex_key);
		bool status;
		if (exists (transaction_a, unchecked_key_a))
		{
			status = dht.update (hex_key.c_str (), (const DHT_val<uint8_t, sizeof_unchecked_info> &)unchecked_dht_val (value_a));
		}
		else
		{
			status = dht.insert (hex_key.c_str (), (const DHT_val<uint8_t, sizeof_unchecked_info> &)unchecked_dht_val (value_a));
		}
		release_assert (status);
	}

	virtual bool exists (nano::transaction const &, nano::unchecked_key const & unchecked_key_a) override
	{
		std::string hex_key;
		unchecked_key_a.encode_hex (hex_key);
		auto status (dht.lookup (hex_key.c_str ()));
		return status;
	};

	virtual void del (nano::write_transaction const &, nano::unchecked_key const & unchecked_key_a) override
	{
		std::string hex_key;
		unchecked_key_a.encode_hex (hex_key);
		auto status (dht.remove (hex_key.c_str ()));
		if (!status)
		{
			auto lookup_status (dht.lookup (hex_key.c_str ()));
			release_assert (!lookup_status);
		}
	}

	virtual size_t count (nano::transaction const &) override
	{
		auto size (dht.size ());
		return size;
	}

private:
	std::unique_ptr<dht::DiskHash<unchecked_info_dht>> dht_impl;
	dht::DiskHash<unchecked_info_dht> & dht;
};

template <typename T, typename U, typename V, typename W>
class dht_iterator;

/**
 * dht implementation of the block store
 */
class dht_mdb_store : public mdb_store
{
	friend class nano::unchecked_dht_mdb_store;

private:
	nano::unchecked_dht_mdb_store unchecked_dht_mdb_store;

public:
	explicit dht_mdb_store (
	nano::logger_mt & logger_a,
	boost::filesystem::path const & mdb_path_a,
	boost::filesystem::path const & dht_path_a,
	nano::ledger_constants & constants,
	nano::txn_tracking_config const & txn_tracking_config_a = nano::txn_tracking_config{},
	std::chrono::milliseconds block_processor_batch_max_time_a = std::chrono::milliseconds (5000),
	nano::lmdb_config const & lmdb_config_a = nano::lmdb_config{},
	nano::dht_config const & dht_config_a = nano::dht_config{},
	bool backup_before_upgrade_a = false) :
		mdb_store{
			logger_a,
			mdb_path_a,
			constants,
			txn_tracking_config_a,
			block_processor_batch_max_time_a,
			lmdb_config_a,
			backup_before_upgrade_a
		},
		unchecked_dht_mdb_store{ *this, dht_path_a } {};

	template <typename Key, typename Value>
	nano::store_iterator<Key, Value> make_iterator (nano::transaction const & transaction_a, tables table_a, bool const direction_asc) const
	{
		release_assert (direction_asc);
		if (table_a == tables::unchecked)
		{
			return nano::store_iterator<Key, Value> (std::make_unique<nano::dht_iterator<Key, Value, unchecked_key_dht, unchecked_info_dht>> (unchecked_dht_mdb_store.dht));
		}
		return nano::store_iterator<Key, Value> (std::make_unique<nano::mdb_iterator<Key, Value>> (transaction_a, table_to_dbi (table_a), nano::mdb_val{}, direction_asc));
	}

	template <typename Key, typename Value>
	nano::store_iterator<Key, Value> make_iterator (nano::transaction const & transaction_a, tables table_a, nano::mdb_val const & key) const
	{
		if (table_a == tables::unchecked)
		{
			return nano::store_iterator<Key, Value> (std::make_unique<nano::dht_iterator<Key, Value, unchecked_key_dht, unchecked_info_dht>> (unchecked_dht_mdb_store.dht));
		}
		return nano::store_iterator<Key, Value> (std::make_unique<nano::mdb_iterator<Key, Value>> (transaction_a, table_to_dbi (table_a), key));
	}

private:
	int get (nano::transaction const & transaction_a, tables table_a, nano::mdb_val const & key_a, nano::mdb_val & value_a) const
	{
		std::string hex_key;
		nano::unchecked_key key (key_a);
		nano::unchecked_info_dht * read_value;
		key.encode_hex (hex_key);
		if (table_a == tables::unchecked)
		{
			read_value = unchecked_dht_mdb_store.dht.lookup (hex_key.c_str ());
			if (read_value != nullptr)
			{
				return 0;
			}
			return MDB_NOTFOUND;
		}
		return mdb_get (env.tx (transaction_a), table_to_dbi (table_a), key_a, value_a);
	}

	int drop (nano::write_transaction const & transaction_a, tables table_a) override
	{
		if (table_a == tables::unchecked)
		{
			unchecked_dht_mdb_store.clear (transaction_a);
			return 0;
		}
		else
		{
			return mdb_store::clear (transaction_a, mdb_store::table_to_dbi (table_a));
		}
	}
};

template <>
void * unchecked_dht_val::data () const;
template <>
size_t unchecked_dht_val::size () const;
template <>
unchecked_dht_val::db_val (size_t size_a, void * data_a);
template <>
void unchecked_dht_val::convert_buffer_to_value ();

extern template class store_partial<nano::mdb_val, dht_mdb_store>;
extern template class store_partial<nano::unchecked_dht_val, dht_mdb_store>;
}
