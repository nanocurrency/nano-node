#pragma once

#include <nano/lib/diagnosticsconfig.hpp>
#include <nano/lib/lmdbconfig.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/secure/common.hpp>
#include <nano/store/db_val.hpp>
#include <nano/store/lmdb/account.hpp>
#include <nano/store/lmdb/block.hpp>
#include <nano/store/lmdb/confirmation_height.hpp>
#include <nano/store/lmdb/db_val.hpp>
#include <nano/store/lmdb/final_vote.hpp>
#include <nano/store/lmdb/iterator.hpp>
#include <nano/store/lmdb/lmdb_env.hpp>
#include <nano/store/lmdb/online_weight.hpp>
#include <nano/store/lmdb/peer.hpp>
#include <nano/store/lmdb/pending.hpp>
#include <nano/store/lmdb/pruned.hpp>
#include <nano/store/lmdb/rep_weight.hpp>
#include <nano/store/lmdb/transaction_impl.hpp>
#include <nano/store/lmdb/version.hpp>
#include <nano/store/versioning.hpp>

#include <boost/optional.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace nano
{
class logging_mt;

}

namespace nano::store::lmdb
{
/**
 * mdb implementation of the block store
 */
class component : public nano::store::component
{
private:
	nano::store::lmdb::account account_store;
	nano::store::lmdb::block block_store;
	nano::store::lmdb::confirmation_height confirmation_height_store;
	nano::store::lmdb::final_vote final_vote_store;
	nano::store::lmdb::online_weight online_weight_store;
	nano::store::lmdb::peer peer_store;
	nano::store::lmdb::pending pending_store;
	nano::store::lmdb::pruned pruned_store;
	nano::store::lmdb::version version_store;
	nano::store::lmdb::rep_weight rep_weight_store;

	friend class nano::store::lmdb::account;
	friend class nano::store::lmdb::block;
	friend class nano::store::lmdb::confirmation_height;
	friend class nano::store::lmdb::final_vote;
	friend class nano::store::lmdb::online_weight;
	friend class nano::store::lmdb::peer;
	friend class nano::store::lmdb::pending;
	friend class nano::store::lmdb::pruned;
	friend class nano::store::lmdb::version;
	friend class nano::store::lmdb::rep_weight;

public:
	component (nano::logger &, std::filesystem::path const &, nano::ledger_constants & constants, nano::txn_tracking_config const & txn_tracking_config_a = nano::txn_tracking_config{}, std::chrono::milliseconds block_processor_batch_max_time_a = std::chrono::milliseconds (5000), nano::lmdb_config const & lmdb_config_a = nano::lmdb_config{}, bool backup_before_upgrade = false);
	store::write_transaction tx_begin_write () override;
	store::read_transaction tx_begin_read () const override;

	std::string vendor_get () const override;

	void serialize_mdb_tracker (boost::property_tree::ptree &, std::chrono::milliseconds, std::chrono::milliseconds) override;

	static void create_backup_file (nano::store::lmdb::env &, std::filesystem::path const &, nano::logger &);

	void serialize_memory_stats (boost::property_tree::ptree &) override;

	unsigned max_block_write_batch_num () const override;

private:
	nano::logger & logger;
	bool error{ false };

public:
	nano::store::lmdb::env env;

	bool exists (store::transaction const & transaction_a, tables table_a, nano::store::lmdb::db_val const & key_a) const;

	int get (store::transaction const & transaction_a, tables table_a, nano::store::lmdb::db_val const & key_a, nano::store::lmdb::db_val & value_a) const;
	int put (store::write_transaction const & transaction_a, tables table_a, nano::store::lmdb::db_val const & key_a, nano::store::lmdb::db_val const & value_a) const;
	int del (store::write_transaction const & transaction_a, tables table_a, nano::store::lmdb::db_val const & key_a) const;

	bool copy_db (std::filesystem::path const & destination_file) override;
	void rebuild_db (store::write_transaction const & transaction_a) override;

	bool init_error () const override;

	uint64_t count (store::transaction const &, MDB_dbi) const;
	std::string error_string (int status) const override;

private:
	bool do_upgrades (store::write_transaction &, nano::ledger_constants & constants, bool &);
	void upgrade_v21_to_v22 (store::write_transaction &);
	void upgrade_v22_to_v23 (store::write_transaction &);
	void upgrade_v23_to_v24 (store::write_transaction &);

	void open_databases (bool &, store::transaction const &, unsigned);

	int drop (store::write_transaction const & transaction_a, tables table_a) override;
	int clear (store::write_transaction const & transaction_a, MDB_dbi handle_a);

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
	nano::store::lmdb::txn_callbacks create_txn_callbacks () const;
	bool txn_tracking_enabled;

	uint64_t count (store::transaction const & transaction_a, tables table_a) const override;

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
	friend class mdb_block_store_upgrade_v21_v22_Test;
	friend class block_store_DISABLED_change_dupsort_Test;
};
} // namespace nano::store::lmdb
