#include <gtest/gtest.h>

#include <nano/secure/blockstore.hpp>
#include <nano/secure/versioning.hpp>

TEST (versioning, account_info_v1)
{
	auto file (nano::unique_path ());
	nano::account account (1);
	nano::open_block open (1, 2, 3, nullptr);
	nano::account_info_v1 v1 (open.hash (), open.hash (), 3, 4);
	{
		nano::logging logging;
		auto error (false);
		nano::mdb_store store (error, logging, file);
		ASSERT_FALSE (error);
		store.stop ();
		auto transaction (store.tx_begin (true));
		nano::block_sideband sideband (nano::block_type::open, 0, 0, 0, 0, 0);
		store.block_put (transaction, open.hash (), open, sideband);
		auto status (mdb_put (store.env.tx (transaction), store.accounts_v0, nano::mdb_val (account), v1.val (), 0));
		ASSERT_EQ (0, status);
		store.version_put (transaction, 1);
	}
	{
		nano::logging logging;
		auto error (false);
		nano::mdb_store store (error, logging, file);
		ASSERT_FALSE (error);
		auto transaction (store.tx_begin ());
		nano::account_info v2;
		ASSERT_FALSE (store.account_get (transaction, account, v2));
		ASSERT_EQ (open.hash (), v2.open_block);
		ASSERT_EQ (v1.balance, v2.balance);
		ASSERT_EQ (v1.head, v2.head);
		ASSERT_EQ (v1.modified, v2.modified);
		ASSERT_EQ (v1.rep_block, v2.rep_block);
	}
}
