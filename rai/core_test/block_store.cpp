#include <gtest/gtest.h>
#include <rai/node.hpp>

#include <fstream>

TEST (block_store, construction)
{
    bool init (false);
    rai::block_store db (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    auto now (db.now ());
    ASSERT_GT (now, 1408074640);
}

TEST (block_store, add_item)
{
    bool init (false);
    rai::block_store db (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::open_block block (0, 0, 0, 0, 0, 0);
    rai::uint256_union hash1 (block.hash ());
    auto latest1 (db.block_get (hash1));
    ASSERT_EQ (nullptr, latest1);
    ASSERT_FALSE (db.block_exists (hash1));
    db.block_put (hash1, block);
    auto latest2 (db.block_get (hash1));
    ASSERT_NE (nullptr, latest2);
    ASSERT_EQ (block, *latest2);
    ASSERT_TRUE (db.block_exists (hash1));
	db.block_del (hash1);
	auto latest3 (db.block_get (hash1));
	ASSERT_EQ (nullptr, latest3);
}

TEST (block_store, add_nonempty_block)
{
    bool init (false);
    rai::block_store db (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::keypair key1;
    rai::open_block block (0, 0, 0, 0, 0, 0);
    rai::uint256_union hash1 (block.hash ());
    block.signature = rai::sign_message (key1.prv, key1.pub, hash1);
    auto latest1 (db.block_get (hash1));
    ASSERT_EQ (nullptr, latest1);
    db.block_put (hash1, block);
    auto latest2 (db.block_get (hash1));
    ASSERT_NE (nullptr, latest2);
    ASSERT_EQ (block, *latest2);
}

TEST (block_store, add_two_items)
{
    bool init (false);
    rai::block_store db (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::keypair key1;
    rai::open_block block (1, 0, 0, 0, 0, 0);
    rai::uint256_union hash1 (block.hash ());
    block.signature = rai::sign_message (key1.prv, key1.pub, hash1);
    auto latest1 (db.block_get (hash1));
    ASSERT_EQ (nullptr, latest1);
    rai::open_block block2 (3, 0, 0, 0, 0, rai::work_generate (3));
    block2.hashables.account = 3;
    rai::uint256_union hash2 (block2.hash ());
    block2.signature = rai::sign_message (key1.prv, key1.pub, hash2);
    auto latest2 (db.block_get (hash2));
    ASSERT_EQ (nullptr, latest2);
    db.block_put (hash1, block);
    db.block_put (hash2, block2);
    auto latest3 (db.block_get (hash1));
    ASSERT_NE (nullptr, latest3);
    ASSERT_EQ (block, *latest3);
    auto latest4 (db.block_get (hash2));
    ASSERT_NE (nullptr, latest4);
    ASSERT_EQ (block2, *latest4);
    ASSERT_FALSE (*latest3 == *latest4);
}

TEST (block_store, add_receive)
{
    bool init (false);
    rai::block_store db (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::keypair key1;
    rai::keypair key2;
	rai::open_block block1 (0, 0, 0, 0, 0, 0);
	db.block_put (block1.hash (), block1);
    rai::receive_block block (block1.hash (), 1, 1, 2, 3);
    rai::block_hash hash1 (block.hash ());
    auto latest1 (db.block_get (hash1));
    ASSERT_EQ (nullptr, latest1);
    db.block_put (hash1, block);
    auto latest2 (db.block_get (hash1));
    ASSERT_NE (nullptr, latest2);
    ASSERT_EQ (block, *latest2);
}

TEST (block_store, add_pending)
{
    bool init (false);
    rai::block_store db (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::keypair key1;
    rai::block_hash hash1 (0);
    rai::receivable receivable1;
    ASSERT_TRUE (db.pending_get (hash1, receivable1));
    db.pending_put (hash1, receivable1);
    rai::receivable receivable2;
    ASSERT_FALSE (db.pending_get (hash1, receivable2));
    ASSERT_EQ (receivable1, receivable2);
    db.pending_del (hash1);
    ASSERT_TRUE (db.pending_get (hash1, receivable2));
}

TEST (block_store, pending_iterator)
{
    bool init (false);
    rai::block_store db (init, rai::unique_path ());
    ASSERT_TRUE (!init);
	rai::transaction transaction (db.environment, nullptr, false);
    ASSERT_EQ (db.pending_end (), db.pending_begin (transaction));
    db.pending_put (1, {2, 3, 4});
    auto current (db.pending_begin (transaction));
    ASSERT_NE (db.pending_end (), current);
    ASSERT_EQ (rai::account (1), current->first);
	rai::receivable receivable (current->second);
    ASSERT_EQ (rai::account (2), receivable.source);
    ASSERT_EQ (rai::amount (3), receivable.amount);
    ASSERT_EQ (rai::account (4), receivable.destination);
}

TEST (block_store, genesis)
{
    bool init (false);
    rai::block_store db (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::genesis genesis;
    auto hash (genesis.hash ());
    genesis.initialize (db);
    rai::frontier frontier;
    ASSERT_FALSE (db.latest_get (rai::genesis_account, frontier));
	ASSERT_EQ (hash, frontier.hash);
    auto block1 (db.block_get (frontier.hash));
    ASSERT_NE (nullptr, block1);
    auto receive1 (dynamic_cast <rai::open_block *> (block1.get ()));
    ASSERT_NE (nullptr, receive1);
    ASSERT_LE (frontier.time, db.now ());
	auto test_pub_text (rai::test_genesis_key.pub.to_string ());
	auto test_pub_account (rai::test_genesis_key.pub.to_base58check ());
	auto test_prv_text (rai::test_genesis_key.prv.to_string ());
	ASSERT_EQ (rai::genesis_account, rai::test_genesis_key.pub);
}

TEST (representation, changes)
{
    bool init (false);
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::keypair key1;
    ASSERT_EQ (0, store.representation_get (key1.pub));
    store.representation_put (key1.pub, 1);
    ASSERT_EQ (1, store.representation_get (key1.pub));
    store.representation_put (key1.pub, 2);
    ASSERT_EQ (2, store.representation_get (key1.pub));
}

TEST (bootstrap, simple)
{
    bool init (false);
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::send_block block1 (0, 1, 2, 3, 4, 5);
    auto block2 (store.unchecked_get (block1.previous ()));
    ASSERT_EQ (nullptr, block2);
    store.unchecked_put (block1.previous (), block1);
    auto block3 (store.unchecked_get (block1.previous ()));
    ASSERT_NE (nullptr, block3);
    ASSERT_EQ (block1, *block3);
    store.unchecked_del (block1.previous ());
    auto block4 (store.unchecked_get (block1.previous ()));
    ASSERT_EQ (nullptr, block4);
}

TEST (checksum, simple)
{
    bool init (false);
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::block_hash hash0 (0);
    ASSERT_TRUE (store.checksum_get (0x100, 0x10, hash0));
    rai::block_hash hash1 (0);
    store.checksum_put (0x100, 0x10, hash1);
    rai::block_hash hash2;
    ASSERT_FALSE (store.checksum_get (0x100, 0x10, hash2));
    ASSERT_EQ (hash1, hash2);
    store.checksum_del (0x100, 0x10);
    rai::block_hash hash3;
    ASSERT_TRUE (store.checksum_get (0x100, 0x10, hash3));
}

TEST (block_store, empty_blocks)
{
    bool init (false);
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
	rai::transaction transaction (store.environment, nullptr, false);
    auto begin (store.blocks_begin (transaction));
    auto end (store.blocks_end ());
    ASSERT_EQ (end, begin);
}

TEST (block_store, empty_accounts)
{
    bool init (false);
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
	rai::transaction transaction (store.environment, nullptr, false);
    auto begin (store.latest_begin (transaction));
    auto end (store.latest_end ());
    ASSERT_EQ (end, begin);
}

TEST (block_store, one_block)
{
    bool init (false);
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::open_block block1 (0, 0, 0, 0, 0, rai::work_generate (0));
    store.block_put (block1.hash (), block1);
	rai::transaction transaction (store.environment, nullptr, false);
    auto begin (store.blocks_begin (transaction));
    auto end (store.blocks_end ());
    ASSERT_NE (end, begin);
    auto hash1 (begin->first);
    ASSERT_EQ (block1.hash (), hash1);
    auto block2 (rai::deserialize_block (begin->second));
    ASSERT_EQ (block1, *block2);
    ++begin;
    ASSERT_EQ (end, begin);
}

TEST (block_store, empty_bootstrap)
{
    bool init (false);
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
	rai::transaction transaction (store.environment, nullptr, false);
    auto begin (store.unchecked_begin (transaction));
    auto end (store.unchecked_end ());
    ASSERT_EQ (end, begin);
}

TEST (block_store, one_bootstrap)
{
    bool init (false);
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::send_block block1 (0, 1, 2, 3, 4, 5);
    store.unchecked_put (block1.hash (), block1);
	rai::transaction transaction (store.environment, nullptr, false);
    auto begin (store.unchecked_begin (transaction));
    auto end (store.unchecked_end ());
    ASSERT_NE (end, begin);
    auto hash1 (begin->first);
    ASSERT_EQ (block1.hash (), hash1);
    auto block2 (rai::deserialize_block (begin->second));
    ASSERT_EQ (block1, *block2);
    ++begin;
    ASSERT_EQ (end, begin);
}

TEST (block_store, frontier_retrieval)
{
    bool init (false);
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::account account1 (0);
    rai::frontier frontier1 (0, 0, 0, 0);
    store.latest_put (account1, frontier1);
    rai::frontier frontier2;
    store.latest_get (account1, frontier2);
    ASSERT_EQ (frontier1, frontier2);
}

TEST (block_store, one_account)
{
    bool init (false);
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::account account (0);
    rai::block_hash hash (0);
    store.latest_put (account, {hash, account, 42, 100});
	rai::transaction transaction (store.environment, nullptr, false);
    auto begin (store.latest_begin (transaction));
    auto end (store.latest_end ());
    ASSERT_NE (end, begin);
    ASSERT_EQ (account, begin->first);
	rai::frontier frontier (begin->second);
    ASSERT_EQ (hash, frontier.hash);
    ASSERT_EQ (42, frontier.balance.number ());
    ASSERT_EQ (100, frontier.time);
    ++begin;
    ASSERT_EQ (end, begin);
}

TEST (block_store, two_block)
{
    bool init (false);
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::open_block block1 (1, 0, 0, 0, 0, 0);
    block1.hashables.account = 1;
    std::vector <rai::block_hash> hashes;
    std::vector <rai::open_block> blocks;
    hashes.push_back (block1.hash ());
    blocks.push_back (block1);
    store.block_put (hashes [0], block1);
    rai::open_block block2 (2, 0, 0, 0, 0, 0);
    hashes.push_back (block2.hash ());
    blocks.push_back (block2);
    store.block_put (hashes [1], block2);
	rai::transaction transaction (store.environment, nullptr, false);
    auto begin (store.blocks_begin (transaction));
    auto end (store.blocks_end ());
    ASSERT_NE (end, begin);
    auto hash1 (begin->first);
    ASSERT_NE (hashes.end (), std::find (hashes.begin (), hashes.end (), hash1));
    auto block3 (rai::deserialize_block (begin->second));
    ASSERT_NE (blocks.end (), std::find (blocks.begin (), blocks.end (), *block3));
    ++begin;
    ASSERT_NE (end, begin);
    auto hash2 (begin->first);
    ASSERT_NE (hashes.end (), std::find (hashes.begin (), hashes.end (), hash2));
    auto block4 (rai::deserialize_block (begin->second));
    ASSERT_NE (blocks.end (), std::find (blocks.begin (), blocks.end (), *block4));
    ++begin;
    ASSERT_EQ (end, begin);
}

TEST (block_store, two_account)
{
    bool init (false);
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::account account1 (1);
    rai::block_hash hash1 (2);
    rai::account account2 (3);
    rai::block_hash hash2 (4);
    store.latest_put (account1, {hash1, account1, 42, 100});
    store.latest_put (account2, {hash2, account2, 84, 200});
	rai::transaction transaction (store.environment, nullptr, false);
    auto begin (store.latest_begin (transaction));
    auto end (store.latest_end ());
    ASSERT_NE (end, begin);
    ASSERT_EQ (account1, begin->first);
	rai::frontier frontier1 (begin->second);
    ASSERT_EQ (hash1, frontier1.hash);
    ASSERT_EQ (42, frontier1.balance.number ());
    ASSERT_EQ (100, frontier1.time);
    ++begin;
    ASSERT_NE (end, begin);
    ASSERT_EQ (account2, begin->first);
	rai::frontier frontier2 (begin->second);
    ASSERT_EQ (hash2, frontier2.hash);
    ASSERT_EQ (84, frontier2.balance.number ());
    ASSERT_EQ (200, frontier2.time);
    ++begin;
    ASSERT_EQ (end, begin);
}

TEST (block_store, latest_find)
{
    bool init (false);
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::account account1 (1);
    rai::block_hash hash1 (2);
    rai::account account2 (3);
    rai::block_hash hash2 (4);
    store.latest_put (account1, {hash1, account1, 100, 0});
    store.latest_put (account2, {hash2, account2, 200, 0});
	rai::transaction transaction (store.environment, nullptr, false);
    auto first (store.latest_begin (transaction));
    auto second (store.latest_begin (transaction));
    ++second;
    auto find1 (store.latest_begin (transaction, 1));
    ASSERT_EQ (first, find1);
    auto find2 (store.latest_begin (transaction, 3));
    ASSERT_EQ (second, find2);
    auto find3 (store.latest_begin (transaction, 2));
    ASSERT_EQ (second, find3);
}

TEST (block_store, bad_path)
{
    bool init (false);
    rai::block_store store (init, boost::filesystem::path {});
    ASSERT_FALSE (!init);
}

TEST (block_store, already_open)
{
    auto path (rai::unique_path ());
    boost::filesystem::create_directories (path);
    std::ofstream file;
    file.open ((path / "accounts.ldb").string ().c_str ());
    ASSERT_TRUE (file.is_open ());
    bool init (false);
    rai::block_store store (init, path);
    ASSERT_FALSE (init);
}

TEST (block_store, delete_iterator_entry)
{
    bool init (false);
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::open_block block1 (1, 0, 0, 0, 0, 0);
    store.block_put (block1.hash (), block1);
    rai::open_block block2 (2, 0, 0, 0, 0, 0);
    store.block_put (block2.hash (), block2);
	rai::transaction transaction (store.environment, nullptr, false);
    auto current (store.blocks_begin (transaction));
    ASSERT_NE (store.blocks_end (), current);
    store.block_del (current->first);
    ++current;
    ASSERT_NE (store.blocks_end (), current);
    store.block_del (current->first);
    ++current;
    ASSERT_EQ (store.blocks_end (), current);
}

TEST (block_store, roots)
{
	bool init (false);
	rai::block_store store (init, rai::unique_path ());
	ASSERT_TRUE (!init);
	rai::send_block send_block (0, 1, 2, 3, 4, 5);
	ASSERT_EQ (send_block.hashables.previous, send_block.root ());
	rai::change_block change_block (0, 1, 2, 3, 4);
	ASSERT_EQ (change_block.hashables.previous, change_block.root ());
	rai::receive_block receive_block (0, 1, 2, 3, 4);
	ASSERT_EQ (receive_block.hashables.previous, receive_block.root ());
	rai::open_block open_block (0, 1, 2, 3, 4, 5);
	ASSERT_EQ (open_block.hashables.account, open_block.root ());
}

TEST (block_store, pending_exists)
{
    bool init (false);
    rai::block_store store (init, rai::unique_path ());
	ASSERT_TRUE (!init);
    rai::block_hash two (2);
    rai::receivable receivable;
    store.pending_put (two, receivable);
    rai::block_hash one (1);
    ASSERT_FALSE (store.pending_exists (one));
}

TEST (block_store, latest_exists)
{
    bool init (false);
    rai::block_store store (init, rai::unique_path ());
	ASSERT_TRUE (!init);
    rai::block_hash two (2);
    rai::frontier frontier;
    store.latest_put (two, frontier);
    rai::block_hash one (1);
    ASSERT_FALSE (store.latest_exists (one));
}

TEST (block_store, stack)
{
    bool init (false);
    rai::block_store store (init, rai::unique_path ());
	ASSERT_TRUE (!init);
    rai::block_hash hash1 (1);
    store.stack_push (0, hash1);
    rai::block_hash hash2 (2);
    store.stack_push (1, hash2);
    auto hash3 (store.stack_pop (1));
    ASSERT_EQ (hash2, hash3);
    auto hash4 (store.stack_pop (0));
    ASSERT_EQ (hash1, hash4);
}

TEST (block_store, unsynced)
{
    bool init (false);
    rai::block_store store (init, rai::unique_path ());
	ASSERT_TRUE (!init);
	rai::transaction transaction (store.environment, nullptr, false);
    ASSERT_EQ (store.unsynced_end (), store.unsynced_begin (transaction));
    rai::block_hash hash1 (0);
    ASSERT_FALSE (store.unsynced_exists (transaction, hash1));
    store.unsynced_put (hash1);
    ASSERT_TRUE (store.unsynced_exists (transaction, hash1));
    ASSERT_NE (store.unsynced_end (), store.unsynced_begin (transaction));
    ASSERT_EQ (hash1, rai::uint256_union (store.unsynced_begin (transaction)->first));
    store.unsynced_del (hash1);
    ASSERT_FALSE (store.unsynced_exists (transaction, hash1));
    ASSERT_EQ (store.unsynced_end (), store.unsynced_begin (transaction));
}

TEST (block_store, unsynced_iteration)
{
    bool init (false);
    rai::block_store store (init, rai::unique_path ());
	ASSERT_TRUE (!init);
	rai::transaction transaction (store.environment, nullptr, false);
    ASSERT_EQ (store.unsynced_end (), store.unsynced_begin (transaction));
    rai::block_hash hash1 (1);
    store.unsynced_put (hash1);
    rai::block_hash hash2 (2);
    store.unsynced_put (hash2);
    std::unordered_set <rai::block_hash> hashes;
    for (auto i (store.unsynced_begin (transaction)), n (store.unsynced_end ()); i != n; ++i)
    {
        hashes.insert (rai::uint256_union (i->first));
    }
    ASSERT_EQ (2, hashes.size ());
    ASSERT_TRUE (hashes.find (hash1) != hashes.end ());
    ASSERT_TRUE (hashes.find (hash2) != hashes.end ());
}