#pragma once

// TODO: keep this until diskhash builds fine on Windows
#ifndef _WIN32

#include <nano/lib/config.hpp>
#include <nano/lib/lmdbconfig.hpp>
#include <nano/lib/logger_mt.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/node/dht/dht_definitions.hpp>
#include <nano/node/dht/dht_iterator.hpp>
#include <nano/node/lmdb/lmdb.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/store/unchecked_store_partial.hpp>
#include <nano/secure/store_partial.hpp>

#include <boost/format.hpp>
#include <boost/pointer_cast.hpp>
#include <boost/polymorphic_cast.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>

#include <diskhash.hpp>

namespace nano
{
class logging_mt;
class dht_config;
class dht_mdb_store;
class unchecked_dht_iterator;

class unchecked_dht_mdb_store : public unchecked_mdb_store
{
	friend class nano::dht_mdb_store;

private:
	nano::dht_mdb_store & local_dht_mdb_store;

public:
	explicit unchecked_dht_mdb_store (
	nano::dht_mdb_store & dht_mdb_store_a,
	boost::filesystem::path const & dht_path_a);

	void clear (nano::write_transaction const &) override;

	nano::store_iterator<nano::unchecked_key, nano::unchecked_info> end () const override;
	nano::store_iterator<nano::unchecked_key, nano::unchecked_info> begin (nano::transaction const & transaction_a) const override;
	nano::store_iterator<nano::unchecked_key, nano::unchecked_info> begin (nano::transaction const & transaction_a, nano::unchecked_key const & key_a) const override;

	virtual std::vector<nano::unchecked_info> get (nano::transaction const & transaction_a, nano::block_hash const & hash_a) override
	{
		std::vector<nano::unchecked_info> result;
		for (auto i (begin (transaction_a, nano::unchecked_key (hash_a, 0))), n (end ()); i != n && i->first.key () == hash_a; ++i)
		{
			nano::unchecked_info const & unchecked_info (i->second);
			result.push_back (unchecked_info);
		}
		return result;
	}

private:
	std::unique_ptr<dht::DiskHash<DHT_unchecked_info>> dht_impl;
	dht::DiskHash<DHT_unchecked_info> & dht;
};

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
			return nano::store_iterator<Key, Value> (std::make_unique<nano::unchecked_dht_iterator> (unchecked_dht_mdb_store.dht));
		}
		return nano::store_iterator<Key, Value> (std::make_unique<nano::mdb_iterator<Key, Value>> (transaction_a, table_to_dbi (table_a), nano::mdb_val{}, direction_asc));
	}

	template <typename Key, typename Value>
	nano::store_iterator<Key, Value> make_iterator (nano::transaction const & transaction_a, tables table_a, nano::mdb_val const & key) const
	{
		if (table_a == tables::unchecked)
		{
			return nano::store_iterator<Key, Value> (std::make_unique<nano::unchecked_dht_iterator> (unchecked_dht_mdb_store.dht));
		}
		return nano::store_iterator<Key, Value> (std::make_unique<nano::mdb_iterator<Key, Value>> (transaction_a, table_to_dbi (table_a), key));
	}

	std::string vendor_get () const override
	{
		return boost::str (boost::format ("%1% + Disk-based Hash Table (as experimental for the unchecked blocks)") % mdb_store::vendor_get ());
	}

private:
	uint64_t count (const transaction & transaction_a, tables table_a) const override
	{
		if (table_a != tables::unchecked)
		{
			return mdb_store::count (transaction_a, table_a);
		}
		auto size (unchecked_dht_mdb_store.dht.size ());
		return size;
	}

	bool exists (nano::transaction const & transaction_a, tables table_a, nano::mdb_val const & key_a) const override
	{
		if (table_a != tables::unchecked)
		{
			return mdb_store::exists (transaction_a, table_a, key_a);
		}
		std::string hex_key;
		nano::unchecked_key unchecked_key_a (key_a);
		unchecked_key_a.encode_hex (hex_key);
		auto status (unchecked_dht_mdb_store.dht.lookup (hex_key.c_str ()));
		return status;
	}

	int get (nano::transaction const & transaction_a, tables table_a, nano::mdb_val const & key_a, nano::mdb_val & value_a) const override
	{
		if (table_a != tables::unchecked)
		{
			return mdb_store::get (transaction_a, table_a, key_a, value_a);
		}
		std::string hex_key;
		nano::unchecked_key key (key_a);
		key.encode_hex (hex_key);
		auto * read_value (unchecked_dht_mdb_store.dht.lookup (hex_key.c_str ()));
		if (read_value != nullptr)
		{
			value_a = (nano::unchecked_info)nano::unchecked_info_dht_val (*read_value);
			return 0;
		}
		return MDB_NOTFOUND;
	}

	int put (nano::write_transaction const & transaction_a, tables table_a, nano::mdb_val const & key_a, const nano::mdb_val & value_a) const override
	{
		if (table_a != tables::unchecked)
		{
			return mdb_store::put (transaction_a, table_a, key_a, value_a);
		}
		std::string hex_key;
		nano::unchecked_key unchecked_key_a (key_a);
		unchecked_key_a.encode_hex (hex_key);
		nano::unchecked_info unchecked_info_a (value_a);
		bool status;
		if (exists (transaction_a, tables::unchecked, unchecked_key_a))
		{
			status = unchecked_dht_mdb_store.dht.update (hex_key.c_str (), (const DHT_val<uint8_t, sizeof_unchecked_info> &)unchecked_info_dht_val (unchecked_info_a));
		}
		else
		{
			status = unchecked_dht_mdb_store.dht.insert (hex_key.c_str (), (const DHT_val<uint8_t, sizeof_unchecked_info> &)unchecked_info_dht_val (unchecked_info_a));
		}
		release_assert (status);
		return 0;
	}

	int del (nano::write_transaction const & transaction_a, tables table_a, nano::mdb_val const & key_a) const override
	{
		if (table_a != tables::unchecked)
		{
			return mdb_store::del (transaction_a, table_a, key_a);
		}
		std::string hex_key;
		nano::unchecked_key unchecked_key_a (key_a);
		unchecked_key_a.encode_hex (hex_key);
		auto status (unchecked_dht_mdb_store.dht.remove (hex_key.c_str ()));
		if (!status)
		{
			auto lookup_status (unchecked_dht_mdb_store.dht.lookup (hex_key.c_str ()));
			release_assert (!lookup_status);
		}
		return 0;
	}

	int drop (nano::write_transaction const & transaction_a, tables table_a) override
	{
		if (table_a != tables::unchecked)
		{
			return mdb_store::clear (transaction_a, mdb_store::table_to_dbi (table_a));
		}
		unchecked_dht_mdb_store.clear (transaction_a);
		return 0;
	}
};

template <>
void * unchecked_info_dht_val::data () const;
template <>
size_t unchecked_info_dht_val::size () const;
template <>
unchecked_info_dht_val::db_val (size_t size_a, void * data_a);
template <>
void unchecked_info_dht_val::convert_buffer_to_value ();

extern template class store_partial<nano::mdb_val, dht_mdb_store>;
extern template class store_partial<nano::unchecked_info_dht_val, dht_mdb_store>;
}

#endif // _WIN32 -- TODO: keep this until diskhash builds fine on Windows
