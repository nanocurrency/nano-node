#include <gtest/gtest.h>

#include <rai/core/core.hpp>
#include <fstream>

TEST (wallet, no_key)
{
    bool init;
    rai::wallet_store wallet (init, boost::filesystem::unique_path ());
    ASSERT_FALSE (init);
    rai::keypair key1;
    rai::private_key prv1;
    ASSERT_TRUE (wallet.fetch (key1.pub, prv1));
    ASSERT_TRUE (wallet.valid_password ());
}

TEST (wallet, retrieval)
{
    bool init;
    rai::wallet_store wallet (init, boost::filesystem::unique_path ());
    ASSERT_FALSE (init);
    rai::keypair key1;
    ASSERT_TRUE (wallet.valid_password ());
    wallet.insert (key1.prv);
    rai::private_key prv1;
    ASSERT_FALSE (wallet.fetch (key1.pub, prv1));
    ASSERT_TRUE (wallet.valid_password ());
    ASSERT_EQ (key1.prv, prv1);
    wallet.password.values [0]->bytes [16] ^= 1;
    rai::private_key prv2;
    ASSERT_TRUE (wallet.fetch (key1.pub, prv2));
    ASSERT_FALSE (wallet.valid_password ());
}

TEST (wallet, empty_iteration)
{
    bool init;
    rai::wallet_store wallet (init, boost::filesystem::unique_path ());
    ASSERT_FALSE (init);
    auto i (wallet.begin ());
    auto j (wallet.end ());
    ASSERT_EQ (i, j);
}

TEST (wallet, one_item_iteration)
{
    bool init;
    rai::wallet_store wallet (init, boost::filesystem::unique_path ());
    ASSERT_FALSE (init);
    rai::keypair key1;
    wallet.insert (key1.prv);
    for (auto i (wallet.begin ()), j (wallet.end ()); i != j; ++i)
    {
        ASSERT_EQ (key1.pub, i->first);
        ASSERT_EQ (key1.prv, i->second.prv (wallet.wallet_key (), wallet.salt ().owords [0]));
    }
}

TEST (wallet, two_item_iteration)
{
    bool init;
    rai::wallet_store wallet (init, boost::filesystem::unique_path ());
    ASSERT_FALSE (init);
    rai::keypair key1;
    rai::keypair key2;
    wallet.insert (key1.prv);
    wallet.insert (key2.prv);
    std::set <rai::public_key> keys1;
    std::set <rai::private_key> keys2;
    for (auto i (wallet.begin ()), j (wallet.end ()); i != j; ++i)
    {
        keys1.insert (i->first);
        keys2.insert (i->second.prv (wallet.wallet_key (), wallet.salt ().owords [0]));
    }
    ASSERT_EQ (2, keys1.size ());
    ASSERT_EQ (2, keys2.size ());
    ASSERT_NE (keys1.end (), keys1.find (key1.pub));
    ASSERT_NE (keys2.end (), keys2.find (key1.prv));
    ASSERT_NE (keys1.end (), keys1.find (key2.pub));
    ASSERT_NE (keys2.end (), keys2.find (key2.prv));
}

TEST (wallet, insufficient_spend)
{
    rai::system system (24000, 1);
    rai::keypair key1;
    ASSERT_TRUE (system.wallet (0)->send (key1.pub, 500));
}

TEST (wallet, one_spend)
{
    rai::system system (24000, 1);
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    rai::frontier frontier1;
    system.clients [0]->store.latest_get (rai::test_genesis_key.pub, frontier1);
    rai::keypair key2;
    ASSERT_FALSE (system.wallet (0)->send (key2.pub, std::numeric_limits <rai::uint128_t>::max ()));
    rai::frontier frontier2;
    system.clients [0]->store.latest_get (rai::test_genesis_key.pub, frontier2);
    ASSERT_NE (frontier1, frontier2);
    auto block (system.clients [0]->store.block_get (frontier2.hash));
    ASSERT_NE (nullptr, block);
    ASSERT_EQ (frontier1.hash, block->previous ());
    ASSERT_TRUE (frontier2.balance.is_zero ());
    ASSERT_EQ (0, system.clients [0]->ledger.account_balance (rai::test_genesis_key.pub));
}

TEST (wallet, DISABLED_two_spend)
{/*
    rai::keypair key1;
    rai::keypair key2;
    rai::uint256_union password;
    rai::wallet_store wallet (0, rai::wallet_store_temp);
    wallet.insert (key1.pub, key1.prv, password);
    wallet.insert (key2.pub, key2.prv, password);
    rai::block_store store (rai::block_store_temp);
    rai::ledger ledger (store);
    rai::genesis genesis1 (key1.pub, 100);
    genesis1.initialize (store);
    rai::frontier frontier1;
    ASSERT_FALSE (store.latest_get (key1.pub, frontier1));
    rai::genesis genesis2 (key2.pub, 400);
    genesis2.initialize (store);
    rai::frontier frontier2;
    ASSERT_FALSE (store.latest_get (key2.pub, frontier2));
    rai::keypair key3;
    std::vector <std::unique_ptr <rai::send_block>> blocks;
    ASSERT_FALSE (wallet.generate_send (ledger, key3.pub, 500, password, blocks));
    ASSERT_EQ (2, blocks.size ());
    ASSERT_TRUE (std::all_of (blocks.begin (), blocks.end (), [] (std::unique_ptr <rai::send_block> const & block) {return block->hashables.balance == 0;}));
    ASSERT_TRUE (std::all_of (blocks.begin (), blocks.end (), [key3] (std::unique_ptr <rai::send_block> const & block) {return block->hashables.destination == key3.pub;}));
    ASSERT_TRUE (std::any_of (blocks.begin (), blocks.end (), [key1] (std::unique_ptr <rai::send_block> const & block) {return !rai::validate_message(key1.pub, block->hash (), block->signature);}));
    ASSERT_TRUE (std::any_of (blocks.begin (), blocks.end (), [key2] (std::unique_ptr <rai::send_block> const & block) {return !rai::validate_message(key2.pub, block->hash (), block->signature);}));*/
}

TEST (wallet, partial_spend)
{
    rai::system system (24000, 1);
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    rai::frontier frontier1;
    ASSERT_FALSE (system.clients [0]->store.latest_get (rai::test_genesis_key.pub, frontier1));
    rai::keypair key2;
    ASSERT_FALSE (system.wallet (0)->send (key2.pub, 500));
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max () - 500, system.clients [0]->ledger.account_balance (rai::test_genesis_key.pub));
}

TEST (wallet, spend_no_previous)
{
    rai::system system (24000, 1);
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    rai::frontier frontier1;
    ASSERT_FALSE (system.clients [0]->store.latest_get (rai::test_genesis_key.pub, frontier1));
    for (auto i (0); i < 50; ++i)
    {
        rai::keypair key;
        system.wallet (0)->store.insert (key.prv);
    }
    rai::keypair key2;
    ASSERT_FALSE (system.wallet (0)->send (key2.pub, 500));
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max () - 500, system.clients [0]->ledger.account_balance(rai::test_genesis_key.pub));
}

TEST (wallet, find_none)
{
    bool init1;
    rai::wallet_store wallet (init1, boost::filesystem::unique_path ());
    ASSERT_FALSE (init1);
    rai::uint256_union account;
    ASSERT_EQ (wallet.end (), wallet.find (account));
}

TEST (wallet, find_existing)
{
    bool init;
    rai::wallet_store wallet (init, boost::filesystem::unique_path ());
    ASSERT_FALSE (init);
    rai::keypair key1;
    ASSERT_FALSE (wallet.exists (key1.pub));
    wallet.insert (key1.prv);
    ASSERT_TRUE (wallet.exists (key1.pub));
    auto existing (wallet.find (key1.pub));
    ASSERT_NE (wallet.end (), existing);
    ++existing;
    ASSERT_EQ (wallet.end (), existing);
}

TEST (wallet, rekey)
{
    bool init;
    rai::wallet_store wallet (init, boost::filesystem::unique_path ());
    ASSERT_EQ (wallet.password.value (), wallet.derive_key (""));
    ASSERT_FALSE (init);
    rai::keypair key1;
    wallet.insert (key1.prv);
    rai::uint256_union prv1;
    wallet.fetch (key1.pub, prv1);
    ASSERT_EQ (key1.prv, prv1);
    ASSERT_FALSE (wallet.rekey ("1"));
    ASSERT_EQ (wallet.derive_key ("1"), wallet.password.value ());
    rai::uint256_union prv2;
    wallet.fetch (key1.pub, prv2);
    ASSERT_EQ (key1.prv, prv2);
    *wallet.password.values [0] = 2;
    ASSERT_TRUE (wallet.rekey ("2"));
}

TEST (base58, encode_zero)
{
    rai::uint256_union number0 (0);
    std::string str0;
    number0.encode_base58check (str0);
    ASSERT_EQ (50, str0.size ());
    rai::uint256_union number1;
    ASSERT_FALSE (number1.decode_base58check (str0));
    ASSERT_EQ (number0, number1);
}

TEST (base58, encode_all)
{
    rai::uint256_union number0;
    number0.decode_hex ("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    std::string str0;
    number0.encode_base58check (str0);
    ASSERT_EQ (50, str0.size ());
    rai::uint256_union number1;
    ASSERT_FALSE (number1.decode_base58check (str0));
    ASSERT_EQ (number0, number1);
}

TEST (base58, encode_fail)
{
    rai::uint256_union number0 (0);
    std::string str0;
    number0.encode_base58check (str0);
    str0 [16] ^= 1;
    rai::uint256_union number1;
    ASSERT_TRUE (number1.decode_base58check (str0));
}

TEST (wallet, hash_password)
{
    bool init;
    rai::wallet_store wallet (init, boost::filesystem::unique_path ());
    ASSERT_FALSE (init);
    auto hash1 (wallet.derive_key (""));
    auto hash2 (wallet.derive_key (""));
    ASSERT_EQ (hash1, hash2);
    auto hash3 (wallet.derive_key ("a"));
    ASSERT_NE (hash1, hash3);
}

TEST (fan, reconstitute)
{
    rai::uint256_union value0;
    rai::fan fan (value0, 1024);
    for (auto & i: fan.values)
    {
        ASSERT_NE (value0, *i);
    }
    auto value1 (fan.value ());
    ASSERT_EQ (value0, value1);
}

TEST (fan, change)
{
    rai::uint256_union value0;
    rai::uint256_union value1;
    ASSERT_NE (value0, value1);
    rai::fan fan (value0, 1024);
    ASSERT_EQ (value0, fan.value ());
    fan.value_set (value1);
    ASSERT_EQ (value1, fan.value ());
}

TEST (wallet, bad_path)
{
    bool init;
    rai::wallet_store store (init, boost::filesystem::path {});
    ASSERT_TRUE (init);
}

TEST (wallet, correct)
{
    bool init (true);
    rai::wallet_store store (init, boost::filesystem::unique_path ());
    ASSERT_FALSE (init);
}

TEST (wallet, already_open)
{
    auto path (boost::filesystem::unique_path ());
    boost::filesystem::create_directories (path);
    std::ofstream file;
    file.open ((path / "wallet.ldb").string ().c_str ());
    ASSERT_TRUE (file.is_open ());
    bool init;
    rai::wallet_store store (init, path / "wallet.ldb");
    ASSERT_TRUE (init);
}

TEST (wallet, repoen_default_password)
{
    auto path (boost::filesystem::unique_path ());
    {
        bool init;
        rai::wallet_store wallet (init, path);
        ASSERT_FALSE (init);
        ASSERT_TRUE (wallet.valid_password ());
    }
    {
        bool init;
        rai::wallet_store wallet (init, path);
        ASSERT_FALSE (init);
        ASSERT_TRUE (wallet.valid_password ());
        wallet.enter_password (" ");
        ASSERT_FALSE (wallet.valid_password ());
        wallet.enter_password ("");
        ASSERT_TRUE (wallet.valid_password ());
    }
}

TEST (wallet, representative)
{
    auto error (false);
    rai::wallet_store wallet (error, boost::filesystem::unique_path ());
    ASSERT_FALSE (error);
    ASSERT_FALSE (wallet.is_representative ());
    ASSERT_EQ (rai::genesis_account, wallet.representative ());
    ASSERT_FALSE (wallet.is_representative ());
    rai::keypair key;
    wallet.representative_set (key.pub);
    ASSERT_FALSE (wallet.is_representative ());
    ASSERT_EQ (key.pub, wallet.representative());
    ASSERT_FALSE (wallet.is_representative ());
    wallet.insert (key.prv);
    ASSERT_TRUE (wallet.is_representative ());
}

TEST (wallet, serialize_json_empty)
{
    auto error (false);
    rai::wallet_store wallet1 (error, boost::filesystem::unique_path ());
    std::string serialized;
    wallet1.serialize_json (serialized);
    rai::wallet_store wallet2 (error, boost::filesystem::unique_path (), serialized);
    ASSERT_FALSE (error);
    ASSERT_EQ (wallet1.wallet_key (), wallet2.wallet_key ());
    ASSERT_EQ (wallet1.salt (), wallet2.salt ());
    ASSERT_EQ (wallet1.check (), wallet2.check ());
    ASSERT_EQ (wallet1.representative (), wallet2.representative ());
    ASSERT_EQ (wallet1.end (), wallet1.begin ());
    ASSERT_EQ (wallet2.end (), wallet2.begin ());
}

TEST (wallet, serialize_json_one)
{
    auto error (false);
    rai::wallet_store wallet1 (error, boost::filesystem::unique_path ());
    rai::keypair key;
    wallet1.insert (key.prv);
    std::string serialized;
    wallet1.serialize_json (serialized);
    rai::wallet_store wallet2 (error, boost::filesystem::unique_path (), serialized);
    ASSERT_FALSE (error);
    ASSERT_EQ (wallet1.wallet_key (), wallet2.wallet_key ());
    ASSERT_EQ (wallet1.salt (), wallet2.salt ());
    ASSERT_EQ (wallet1.check (), wallet2.check ());
    ASSERT_EQ (wallet1.representative (), wallet2.representative ());
    ASSERT_TRUE (wallet2.exists (key.pub));
    rai::private_key prv;
    wallet2.fetch (key.pub, prv);
    ASSERT_EQ (key.prv, prv);
}

TEST (wallet, serialize_json_password)
{
    auto error (false);
    rai::wallet_store wallet1 (error, boost::filesystem::unique_path ());
    rai::keypair key;
    wallet1.rekey ("password");
    wallet1.insert (key.prv);
    std::string serialized;
    wallet1.serialize_json (serialized);
    rai::wallet_store wallet2 (error, boost::filesystem::unique_path (), serialized);
    ASSERT_FALSE (error);
    ASSERT_FALSE (wallet2.valid_password ());
    wallet2.enter_password ("password");
    ASSERT_TRUE (wallet2.valid_password ());
    ASSERT_EQ (wallet1.wallet_key (), wallet2.wallet_key ());
    ASSERT_EQ (wallet1.salt (), wallet2.salt ());
    ASSERT_EQ (wallet1.check (), wallet2.check ());
    ASSERT_EQ (wallet1.representative (), wallet2.representative ());
    ASSERT_TRUE (wallet2.exists (key.pub));
    rai::private_key prv;
    wallet2.fetch (key.pub, prv);
    ASSERT_EQ (key.prv, prv);
}

TEST (wallet_store, move)
{
    auto error (false);
    rai::wallet_store wallet1 (error, boost::filesystem::unique_path ());
    ASSERT_FALSE (error);
    rai::keypair key1;
    wallet1.insert (key1.prv);
    rai::wallet_store wallet2 (error, boost::filesystem::unique_path ());
    ASSERT_FALSE (error);
    rai::keypair key2;
    wallet2.insert (key2.prv);
    ASSERT_FALSE (wallet1.exists (key2.pub));
    ASSERT_TRUE (wallet2.exists (key2.pub));
    std::vector <rai::public_key> keys;
    keys.push_back (key2.pub);
    ASSERT_FALSE (wallet1.move (wallet2, keys));
    ASSERT_TRUE (wallet1.exists (key2.pub));
    ASSERT_FALSE (wallet2.exists (key2.pub));
}