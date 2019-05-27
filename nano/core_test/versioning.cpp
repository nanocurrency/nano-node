#include <nano/lib/logger_mt.hpp>
#include <nano/node/lmdb.hpp>
#include <nano/secure/blockstore.hpp>
#include <nano/secure/versioning.hpp>

#include <gtest/gtest.h>

TEST (versioning, account_info_v1)
{
	auto file (nano::unique_path ());
	nano::account account (1);
	nano::open_block open (1, 2, 3, nullptr);
	nano::account_info_v1 v1 (open.hash (), open.hash (), 3, 4);
	{
		nano::logger_mt logger;
		auto error (false);
		nano::mdb_store store (error, logger, file);
		ASSERT_FALSE (error);
		auto transaction (store.tx_begin_write ());
		nano::block_sideband sideband (nano::block_type::open, 0, 0, 0, 0, 0);
		store.block_put (transaction, open.hash (), open, sideband);
		auto status (mdb_put (store.env.tx (transaction), store.accounts_v0, nano::mdb_val (account), nano::mdb_val (sizeof (v1), &v1), 0));
		ASSERT_EQ (0, status);
		store.version_put (transaction, 1);
	}

	nano::logger_mt logger;
	auto error (false);
	nano::mdb_store store (error, logger, file);
	ASSERT_FALSE (error);
	auto transaction (store.tx_begin_read ());
	nano::account_info v_latest;
	ASSERT_FALSE (store.account_get (transaction, account, v_latest));
	ASSERT_EQ (open.hash (), v_latest.open_block);
	ASSERT_EQ (v1.balance, v_latest.balance);
	ASSERT_EQ (v1.head, v_latest.head);
	ASSERT_EQ (v1.modified, v_latest.modified);
	ASSERT_EQ (v1.rep_block, v_latest.rep_block);
	ASSERT_EQ (1, v_latest.block_count);
	ASSERT_EQ (0, v_latest.confirmation_height);
	ASSERT_EQ (nano::epoch::epoch_0, v_latest.epoch);
}

TEST (versioning, account_info_v5)
{
	auto file (nano::unique_path ());
	nano::account account (1);
	nano::open_block open (1, 2, 3, nullptr);
	nano::account_info_v5 v5 (open.hash (), open.hash (), open.hash (), 3, 4);
	{
		nano::logger_mt logger;
		auto error (false);
		nano::mdb_store store (error, logger, file);
		ASSERT_FALSE (error);
		auto transaction (store.tx_begin_write ());
		nano::block_sideband sideband (nano::block_type::open, 0, 0, 0, 0, 0);
		store.block_put (transaction, open.hash (), open, sideband);
		auto status (mdb_put (store.env.tx (transaction), store.accounts_v0, nano::mdb_val (account), nano::mdb_val (sizeof (v5), &v5), 0));
		ASSERT_EQ (0, status);
		store.version_put (transaction, 5);
	}

	nano::logger_mt logger;
	auto error (false);
	nano::mdb_store store (error, logger, file);
	ASSERT_FALSE (error);
	auto transaction (store.tx_begin_read ());
	nano::account_info v_latest;
	ASSERT_FALSE (store.account_get (transaction, account, v_latest));
	ASSERT_EQ (v5.open_block, v_latest.open_block);
	ASSERT_EQ (v5.balance, v_latest.balance);
	ASSERT_EQ (v5.head, v_latest.head);
	ASSERT_EQ (v5.modified, v_latest.modified);
	ASSERT_EQ (v5.rep_block, v_latest.rep_block);
	ASSERT_EQ (1, v_latest.block_count);
	ASSERT_EQ (0, v_latest.confirmation_height);
	ASSERT_EQ (nano::epoch::epoch_0, v_latest.epoch);
}

TEST (versioning, account_info_v13)
{
	auto file (nano::unique_path ());
	nano::account account (1);
	nano::open_block open (1, 2, 3, nullptr);
	nano::account_info_v13 v13 (open.hash (), open.hash (), open.hash (), 3, 4, 10, nano::epoch::epoch_0);
	{
		nano::logger_mt logger;
		auto error (false);
		nano::mdb_store store (error, logger, file);
		ASSERT_FALSE (error);
		auto transaction (store.tx_begin_write ());
		nano::block_sideband sideband (nano::block_type::open, 0, 0, 0, 0, 0);
		store.block_put (transaction, open.hash (), open, sideband);
		auto status (mdb_put (store.env.tx (transaction), store.accounts_v0, nano::mdb_val (account), nano::mdb_val (v13), 0));
		ASSERT_EQ (0, status);
		store.version_put (transaction, 13);
	}

	nano::logger_mt logger;
	auto error (false);
	nano::mdb_store store (error, logger, file);
	ASSERT_FALSE (error);
	auto transaction (store.tx_begin_read ());
	nano::account_info v_latest;
	ASSERT_FALSE (store.account_get (transaction, account, v_latest));
	ASSERT_EQ (v13.open_block, v_latest.open_block);
	ASSERT_EQ (v13.balance, v_latest.balance);
	ASSERT_EQ (v13.head, v_latest.head);
	ASSERT_EQ (v13.modified, v_latest.modified);
	ASSERT_EQ (v13.rep_block, v_latest.rep_block);
	ASSERT_EQ (v13.block_count, v_latest.block_count);
	ASSERT_EQ (0, v_latest.confirmation_height);
	ASSERT_EQ (v13.epoch, v_latest.epoch);
}
