#pragma once

#include <celerix/lib/diagnosticsconfig.hpp>
#include <celerix/lib/lmdbconfig.hpp>
#include <celerix/lib/logging.hpp>
#include <celerix/lib/numbers.hpp>
#include <celerix/secure/common.hpp>
#include <celerix/store/db_val.hpp>
#include <celerix/store/lmdb/account.hpp>
#include <celerix/store/lmdb/block.hpp>
#include <celerix/store/lmdb/confirmation_height.hpp>
#include <celerix/store/lmdb/db_val.hpp>
#include <celerix/store/lmdb/final_vote.hpp>
#include <celerix/store/lmdb/iterator.hpp>
#include <celerix/store/lmdb/lmdb_env.hpp>
#include <celerix/store/lmdb/online_weight.hpp>
#include <celerix/store/lmdb/peer.hpp>
#include <celerix/store/lmdb/pending.hpp>
#include <celerix/store/lmdb/pruned.hpp>
#include <celerix/store/lmdb/rep_weight.hpp>
#include <celerix/store/lmdb/transaction_impl.hpp>
#include <celerix/store/lmdb/version.hpp>
#include <celerix/store/versioning.hpp>

#include <boost/optional.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace celerix
{
class logging_mt;

}

namespace celerix::store::lmdb
{
/**
 * mdb implementation of the block store
 */
class component : public celerix::store::component
{
private:
	celerix::store::lmdb::account account_store;
	celerix::store::lmdb::block block_store;
	celerix::store::lmdb::confirmation_height confirmation_height_store;
	celerix::store::lmdb::final_vote final_vote_store;
	celerix::store::lmdb::online_weight online_weight_store;
	celerix::store::lmdb::peer peer_store;
	celerix::store::lmdb::pending pending_store;
	celerix::store::lmdb::pruned pruned_store;
	celerix::store::lmdb::version version_store;
	celerix::store::lmdb::rep_weight rep_weight_store;

	friend class celerix::store::lmdb::account;
	friend class celerix::store::lmdb::block;
	friend class celerix::store::lmdb::confirmation_height;
	friend class celerix::store::lmdb::final_vote;
	friend class celerix::store::lmdb::online_weight;
	friend class celerix::store::lmdb::peer;
	friend class celerix::store::lmdb::pending;
	friend class celerix::store::lmdb::pruned;
	friend class celerix::store::lmdb::version;
	friend class celerix::store::lmdb::rep_weight;

public:
	component (celerix::logger &, std::filesystem::path const &, celerix::ledger_constants & constants, celerix::txn_tracking_config const & txn_tracking_config_a = celerix::txn_tracking_config{}, std::chrono::milliseconds block_processor_batch_max_time_a = std::chrono::milliseconds (5000), celerix::lmdb_config const & lmdb_config_a = celerix::lmdb_config{}, bool backup_before_upgrade = false);
	store::write_transaction tx_begin_write () override;
	store::read_transaction tx_begin_read () const override;

	std::string vendor_get () const override;

	void serialize_mdb_tracker (boost::property_tree::ptree &, std::chrono::milliseconds, std::chrono::milliseconds) override;

	static void create_backup_file (celerix::store::lmdb::env &, std::filesystem::path const &, celerix::logger &);

	void serialize_memory_stats (boost::property_tree::ptree &) override;

	unsigned max_block_write_batch_num () const override;

private:
	celerix::logger & logger;
	bool error{ false };

public:
	celerix::store::lmdb::env env;

	bool exists (store::transaction const & transaction_a, tables table_a, celerix::store::lmdb::db_val const & key_a) const;

	int get (store::transaction const & transaction_a, tables table_a, celerix::store::lmdb::db_val const & key_a, celerix::store::lmdb::db_val & value_a) const;
	int put (store::write_transaction const & transaction_a, tables table_a, celerix::store::lmdb::db_val const & key_a, celerix::store::lmdb::db_val const & value_a) const;
	int del (store::write_transaction const & transaction_a, tables table_a, celerix::store::lmdb::db_val const & key_a) const;

	bool copy_db (std::filesystem::path const & destination_file) override;
	void rebuild_db (store::write_transaction const & transaction_a) override;

	bool init_error () const override;

	uint64_t count (store::transaction const &, MDB_dbi) const;
	std::string error_string (int status) const override;

private:
	bool do_upgrades (store::write_transaction &, celerix::ledger_constants & constants, bool &);
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

	mutable celerix::mdb_txn_tracker mdb_txn_tracker;
	celerix::store::lmdb::txn_callbacks create_txn_callbacks () const;
	bool txn_tracking_enabled;

	uint64_t count (store::transaction const & transaction_a, tables table_a) const override;

	bool vacuum_after_upgrade (std::filesystem::path const & path_a, celerix::lmdb_config const & lmdb_config_a);

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
} // namespace celerix::store::lmdb
