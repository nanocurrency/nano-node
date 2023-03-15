#pragma once

#include <nano/lib/diagnosticsconfig.hpp>
#include <nano/lib/lmdbconfig.hpp>
#include <nano/lib/logger_mt.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/node/lmdb/account_store.hpp>
#include <nano/node/lmdb/block_store.hpp>
#include <nano/node/lmdb/confirmation_height_store.hpp>
#include <nano/node/lmdb/final_vote_store.hpp>
#include <nano/node/lmdb/frontier_store.hpp>
#include <nano/node/lmdb/lmdb_env.hpp>
#include <nano/node/lmdb/lmdb_iterator.hpp>
#include <nano/node/lmdb/lmdb_txn.hpp>
#include <nano/node/lmdb/online_weight_store.hpp>
#include <nano/node/lmdb/peer_store.hpp>
#include <nano/node/lmdb/pending_store.hpp>
#include <nano/node/lmdb/pruned_store.hpp>
#include <nano/node/lmdb/unchecked_store.hpp>
#include <nano/node/lmdb/version_store.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/versioning.hpp>

#include <boost/optional.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace boost
{
namespace filesystem
{
	class path;
}
}

namespace nano
{
using mdb_val = db_val<MDB_val>;

class logging_mt;
class transaction;

namespace lmdb
{
	/**
 * mdb implementation of the block store
 */
	class store : public nano::store
	{
	private:
		nano::lmdb::account_store account_store;
		nano::lmdb::block_store block_store;
		nano::lmdb::confirmation_height_store confirmation_height_store;
		nano::lmdb::final_vote_store final_vote_store;
		nano::lmdb::frontier_store frontier_store;
		nano::lmdb::online_weight_store online_weight_store;
		nano::lmdb::peer_store peer_store;
		nano::lmdb::pending_store pending_store;
		nano::lmdb::pruned_store pruned_store;
		nano::lmdb::unchecked_store unchecked_store;
		nano::lmdb::version_store version_store;

		friend class nano::lmdb::account_store;
		friend class nano::lmdb::block_store;
		friend class nano::lmdb::confirmation_height_store;
		friend class nano::lmdb::final_vote_store;
		friend class nano::lmdb::frontier_store;
		friend class nano::lmdb::online_weight_store;
		friend class nano::lmdb::peer_store;
		friend class nano::lmdb::pending_store;
		friend class nano::lmdb::pruned_store;
		friend class nano::lmdb::unchecked_store;
		friend class nano::lmdb::version_store;

	public:
		store (nano::logger_mt &, std::filesystem::path const &, nano::ledger_constants & constants, nano::txn_tracking_config const & txn_tracking_config_a = nano::txn_tracking_config{}, std::chrono::milliseconds block_processor_batch_max_time_a = std::chrono::milliseconds (5000), nano::lmdb_config const & lmdb_config_a = nano::lmdb_config{}, bool backup_before_upgrade = false);
		nano::write_transaction tx_begin_write (std::vector<nano::tables> const & tables_requiring_lock = {}, std::vector<nano::tables> const & tables_no_lock = {}) override;
		nano::read_transaction tx_begin_read () const override;

		std::string vendor_get () const override;

		void serialize_mdb_tracker (boost::property_tree::ptree &, std::chrono::milliseconds, std::chrono::milliseconds) override;

		static void create_backup_file (nano::mdb_env &, std::filesystem::path const &, nano::logger_mt &);

		void serialize_memory_stats (boost::property_tree::ptree &) override;

		unsigned max_block_write_batch_num () const override;

	private:
		nano::logger_mt & logger;
		bool error{ false };

	public:
		nano::mdb_env env;

		bool exists (nano::transaction const & transaction_a, tables table_a, nano::mdb_val const & key_a) const;

		int get (nano::transaction const & transaction_a, tables table_a, nano::mdb_val const & key_a, nano::mdb_val & value_a) const;
		int put (nano::write_transaction const & transaction_a, tables table_a, nano::mdb_val const & key_a, nano::mdb_val const & value_a) const;
		int del (nano::write_transaction const & transaction_a, tables table_a, nano::mdb_val const & key_a) const;

		bool copy_db (std::filesystem::path const & destination_file) override;
		void rebuild_db (nano::write_transaction const & transaction_a) override;

		template <typename Key, typename Value>
		nano::store_iterator<Key, Value> make_iterator (nano::transaction const & transaction_a, tables table_a, bool const direction_asc = true) const
		{
			return nano::store_iterator<Key, Value> (std::make_unique<nano::mdb_iterator<Key, Value>> (transaction_a, table_to_dbi (table_a), nano::mdb_val{}, direction_asc));
		}

		template <typename Key, typename Value>
		nano::store_iterator<Key, Value> make_iterator (nano::transaction const & transaction_a, tables table_a, nano::mdb_val const & key) const
		{
			return nano::store_iterator<Key, Value> (std::make_unique<nano::mdb_iterator<Key, Value>> (transaction_a, table_to_dbi (table_a), key));
		}

		bool init_error () const override;

		uint64_t count (nano::transaction const &, MDB_dbi) const;
		std::string error_string (int status) const override;

		// These are only use in the upgrade process.
		std::shared_ptr<nano::block> block_get_v14 (nano::transaction const & transaction_a, nano::block_hash const & hash_a, nano::block_sideband_v14 * sideband_a = nullptr, bool * is_state_v1 = nullptr) const;
		std::size_t block_successor_offset_v14 (nano::transaction const & transaction_a, std::size_t entry_size_a, nano::block_type type_a) const;
		nano::block_hash block_successor_v14 (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const;
		nano::mdb_val block_raw_get_v14 (nano::transaction const & transaction_a, nano::block_hash const & hash_a, nano::block_type & type_a, bool * is_state_v1 = nullptr) const;
		boost::optional<nano::mdb_val> block_raw_get_by_type_v14 (nano::transaction const & transaction_a, nano::block_hash const & hash_a, nano::block_type & type_a, bool * is_state_v1) const;

	private:
		bool do_upgrades (nano::write_transaction &, nano::ledger_constants & constants, bool &);
		void upgrade_v14_to_v15 (nano::write_transaction &);
		void upgrade_v15_to_v16 (nano::write_transaction const &);
		void upgrade_v16_to_v17 (nano::write_transaction const &);
		void upgrade_v17_to_v18 (nano::write_transaction const &, nano::ledger_constants & constants);
		void upgrade_v18_to_v19 (nano::write_transaction const &);
		void upgrade_v19_to_v20 (nano::write_transaction const &);
		void upgrade_v20_to_v21 (nano::write_transaction const &);

		std::shared_ptr<nano::block> block_get_v18 (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const;
		nano::mdb_val block_raw_get_v18 (nano::transaction const & transaction_a, nano::block_hash const & hash_a, nano::block_type & type_a) const;
		boost::optional<nano::mdb_val> block_raw_get_by_type_v18 (nano::transaction const & transaction_a, nano::block_hash const & hash_a, nano::block_type & type_a) const;
		nano::uint128_t block_balance_v18 (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const;

		void open_databases (bool &, nano::transaction const &, unsigned);

		int drop (nano::write_transaction const & transaction_a, tables table_a) override;
		int clear (nano::write_transaction const & transaction_a, MDB_dbi handle_a);

		bool not_found (int status) const override;
		bool success (int status) const override;
		void release_assert_success (int const status) const
		{
			if (!success (status))
			{
				release_assert (false, error_string (status));
			}
		}
		int status_code_not_found () const override;

		MDB_dbi table_to_dbi (tables table_a) const;

		mutable nano::mdb_txn_tracker mdb_txn_tracker;
		nano::mdb_txn_callbacks create_txn_callbacks () const;
		bool txn_tracking_enabled;

		uint64_t count (nano::transaction const & transaction_a, tables table_a) const override;

		bool vacuum_after_upgrade (std::filesystem::path const & path_a, nano::lmdb_config const & lmdb_config_a);

		class upgrade_counters
		{
		public:
			upgrade_counters (uint64_t count_before_v0, uint64_t count_before_v1);
			bool are_equal () const;

			uint64_t before_v0;
			uint64_t before_v1;
			uint64_t after_v0{ 0 };
			uint64_t after_v1{ 0 };
		};

		friend class mdb_block_store_supported_version_upgrades_Test;
		friend class mdb_block_store_upgrade_v14_v15_Test;
		friend class mdb_block_store_upgrade_v15_v16_Test;
		friend class mdb_block_store_upgrade_v16_v17_Test;
		friend class mdb_block_store_upgrade_v17_v18_Test;
		friend class mdb_block_store_upgrade_v18_v19_Test;
		friend class mdb_block_store_upgrade_v19_v20_Test;
		friend class mdb_block_store_upgrade_v20_v21_Test;
		friend class block_store_DISABLED_change_dupsort_Test;
		friend void write_sideband_v14 (nano::lmdb::store &, nano::transaction &, nano::block const &, MDB_dbi);
		friend void write_sideband_v15 (nano::lmdb::store &, nano::transaction &, nano::block const &);
		friend void modify_account_info_to_v14 (nano::lmdb::store &, nano::transaction const &, nano::account const &, uint64_t, nano::block_hash const &);
		friend void modify_confirmation_height_to_v15 (nano::lmdb::store &, nano::transaction const &, nano::account const &, uint64_t);
	};
}

template <>
void * mdb_val::data () const;
template <>
std::size_t mdb_val::size () const;
template <>
mdb_val::db_val (std::size_t size_a, void * data_a);
template <>
void mdb_val::convert_buffer_to_value ();
}
