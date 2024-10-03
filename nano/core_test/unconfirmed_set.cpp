#include <nano/lib/blockbuilders.hpp>
#include <nano/secure/common.hpp>
#include <nano/store/rocksdb/unconfirmed_set.hpp>

#include <gtest/gtest.h>

TEST (unconfirmed_set, construction)
{
	nano::store::unconfirmed_set set;
}

TEST (unconfirmed_set, account_not_exists)
{
	nano::store::unconfirmed_set set;
	auto tx = set.tx_begin_write ();
	nano::account account{ 42 };
	ASSERT_FALSE (set.account.exists (tx, account));
	ASSERT_FALSE (set.account.get (tx, account).has_value ());
}

TEST (unconfirmed_set, account_exists)
{
	nano::store::unconfirmed_set set;
	auto tx = set.tx_begin_write ();
	nano::account account{ 42 };
	nano::account_info info{ 17, 18, 19, 20, 21, 22, nano::epoch::epoch_2 };
	set.account.put (tx, account, info);
	ASSERT_TRUE (set.account.exists (tx, account));
	auto value = set.account.get (tx, account);
	ASSERT_TRUE (value.has_value ());
	ASSERT_EQ (info, value.value ());
	set.account.del (tx, account);
	ASSERT_FALSE (set.account.exists (tx, account));
	ASSERT_FALSE (set.account.get (tx, account).has_value ());
}

TEST (unconfirmed_set, block_not_exists)
{
	nano::store::unconfirmed_set set;
	auto tx = set.tx_begin_write ();
	nano::block_hash hash{ 42 };
	ASSERT_FALSE (set.block.exists (tx, hash));
	ASSERT_EQ (nullptr, set.block.get (tx, hash));
}

TEST (unconfirmed_set, block_exists)
{
	nano::store::unconfirmed_set set;
	auto tx = set.tx_begin_write ();
	nano::state_block_builder builder;
	auto block = builder.make_block ()
	.account (17)
	.representative (18)
	.previous (19)
	.balance (20)
	.link (21)
	.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
	.work (22)
	.build ();
	nano::block_sideband sideband{ 23, 24, 25, 26, 27, nano::block_details{}, nano::epoch::epoch_2 };
	block->sideband_set (sideband);
	auto hash = block->hash ();
	set.block.put (tx, hash, *block);
	ASSERT_TRUE (set.block.exists (tx, hash));
	auto block2 = set.block.get (tx, hash);
	ASSERT_EQ (*block, *block2);
	set.block.del (tx, hash);
	ASSERT_FALSE (set.block.exists (tx, hash));
	ASSERT_EQ (nullptr, set.block.get (tx, hash));
}

TEST (unconfirmed_set, receivable_not_exists)
{
	nano::store::unconfirmed_set set;
	auto tx = set.tx_begin_write ();
	nano::pending_key key{ 42, 43 };
	ASSERT_FALSE (set.receivable.exists (tx, key));
	ASSERT_FALSE (set.receivable.get (tx, key).has_value ());
}

TEST (unconfirmed_set, receivable_exists)
{
	nano::store::unconfirmed_set set;
	auto tx = set.tx_begin_write ();
	nano::pending_key key{ 42, 43 };
	nano::pending_info value{ 44, 45, nano::epoch::epoch_2 };
	set.receivable.put (tx, key, value);
	ASSERT_TRUE (set.receivable.exists (tx, key));
	auto value2 = set.receivable.get (tx, key);
	ASSERT_TRUE (value2.has_value ());
	ASSERT_EQ (value, value2.value ());
	set.receivable.del (tx, key);
	ASSERT_FALSE (set.receivable.exists (tx, key));
	ASSERT_FALSE (set.receivable.get (tx, key).has_value ());
}

TEST (unconfirmed_set, received_not_exists)
{
	nano::store::unconfirmed_set set;
	auto tx = set.tx_begin_write ();
	nano::pending_key key{ 42, 43 };
	ASSERT_FALSE (set.received.exists (tx, key));
}

TEST (unconfirmed_set, received_exists)
{
	nano::store::unconfirmed_set set;
	auto tx = set.tx_begin_write ();
	nano::pending_key key{ 42, 43 };
	set.received.put (tx, key);
	ASSERT_TRUE (set.received.exists (tx, key));
	set.received.del (tx, key);
	ASSERT_FALSE (set.received.exists (tx, key));
}

TEST (unconfirmed_set, successor_not_exists)
{
	nano::store::unconfirmed_set set;
	auto tx = set.tx_begin_write ();
	nano::block_hash key{ 42 };
	nano::block_hash value{ 43 };
	ASSERT_FALSE (set.successor.exists (tx, key));
	ASSERT_FALSE (set.successor.get (tx, key).has_value ());
}

TEST (unconfirmed_set, successor_exists)
{
	nano::store::unconfirmed_set set;
	auto tx = set.tx_begin_write ();
	nano::block_hash key{ 42 };
	nano::block_hash value{ 43 };
	set.successor.put (tx, key, value);
	ASSERT_TRUE (set.successor.exists (tx, key));
	auto value2 = set.successor.get (tx, key);
	ASSERT_TRUE (value2.has_value ());
	ASSERT_EQ (value, value2.value ());
	set.successor.del (tx, key);
	ASSERT_FALSE (set.successor.exists (tx, key));
	ASSERT_FALSE (set.successor.get (tx, key).has_value ());
}

