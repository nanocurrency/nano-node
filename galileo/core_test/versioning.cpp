#include <gtest/gtest.h>

#include <galileo/secure/blockstore.hpp>
#include <galileo/secure/versioning.hpp>

TEST (versioning, account_info_v1)
{
	auto file (galileo::unique_path ());
	galileo::account account (1);
	galileo::open_block open (1, 2, 3, nullptr);
	galileo::account_info_v1 v1 (open.hash (), open.hash (), 3, 4);
	{
		auto error (false);
		galileo::mdb_store store (error, file);
		ASSERT_FALSE (error);
		auto transaction (store.tx_begin (true));
		store.block_put (transaction, open.hash (), open);
		auto status (mdb_put (store.env.tx (transaction), store.accounts_v0, galileo::mdb_val (account), v1.val (), 0));
		ASSERT_EQ (0, status);
		store.version_put (transaction, 1);
	}
	{
		auto error (false);
		galileo::mdb_store store (error, file);
		ASSERT_FALSE (error);
		auto transaction (store.tx_begin ());
		galileo::account_info v2;
		ASSERT_FALSE (store.account_get (transaction, account, v2));
		ASSERT_EQ (open.hash (), v2.open_block);
		ASSERT_EQ (v1.balance, v2.balance);
		ASSERT_EQ (v1.head, v2.head);
		ASSERT_EQ (v1.modified, v2.modified);
		ASSERT_EQ (v1.rep_block, v2.rep_block);
	}
}
