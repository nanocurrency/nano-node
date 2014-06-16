#include <gtest/gtest.h>
#include <boost/thread.hpp>
#include <mu_coin/mu_coin.hpp>

TEST (network, construction)
{
    boost::asio::io_service service;
    mu_coin::processor_service processor;
    mu_coin::client client (service, 24001, processor);
    client.network.receive ();
}

TEST (network, send_keepalive)
{
    boost::asio::io_service service;
    mu_coin::processor_service processor;
    mu_coin::client client1 (service, 24001, processor);
    mu_coin::client client2 (service, 24002, processor);
    client1.network.receive ();
    client2.network.receive ();
    client1.network.send_keepalive (mu_coin::endpoint (boost::asio::ip::address_v4::loopback (), 24002));
    while (client1.network.keepalive_ack_count == 0)
    {
        service.run_one ();
    }
    auto peers1 (client1.peers.list ());
    auto peers2 (client2.peers.list ());
    ASSERT_EQ (1, client1.network.keepalive_ack_count);
    ASSERT_NE (peers1.end (), std::find (peers1.begin (), peers1.end (), mu_coin::endpoint (boost::asio::ip::address_v4::loopback (), 24002)));
    ASSERT_EQ (1, client2.network.keepalive_req_count);
    ASSERT_NE (peers2.end (), std::find (peers2.begin (), peers2.end (), mu_coin::endpoint (boost::asio::ip::address_v4::loopback (), 24001)));
}

TEST (network, publish_req)
{
    auto block (std::unique_ptr <mu_coin::send_block> (new mu_coin::send_block));
    mu_coin::keypair key1;
    mu_coin::keypair key2;
    block->inputs.push_back (mu_coin::send_input (key1.pub, 0, 200));
    block->signatures.push_back (mu_coin::uint512_union ());
    block->outputs.push_back (mu_coin::send_output (key2.pub, 400));
    mu_coin::publish_req req (std::move (block));
    mu_coin::byte_write_stream stream;
    req.serialize (stream);
    mu_coin::publish_req req2;
    mu_coin::byte_read_stream stream2 (stream.data, stream.size);
    auto error (req2.deserialize (stream2));
    ASSERT_FALSE (error);
    ASSERT_EQ (*req.block, *req2.block);
}

TEST (network, send_discarded_publish)
{
    boost::asio::io_service service;
    mu_coin::processor_service processor;
    mu_coin::client client1 (service, 24001, processor);
    mu_coin::client client2 (service, 24002, processor);
    client1.network.receive ();
    client2.network.receive ();
    std::unique_ptr <mu_coin::send_block> block (new mu_coin::send_block);
    block->inputs.push_back (mu_coin::send_input ());
    client1.network.publish_block (client2.network.socket.local_endpoint (), std::move (block));
    while (client2.network.publish_req_count == 0)
    {
        service.run_one ();
    }
    ASSERT_EQ (1, client2.network.publish_req_count);
    ASSERT_EQ (0, client1.network.publish_nak_count);
}

TEST (network, send_invalid_publish)
{
    boost::asio::io_service service;
    mu_coin::processor_service processor;
    mu_coin::client client1 (service, 24001, processor);
    mu_coin::client client2 (service, 24002, processor);
    client1.network.receive ();
    client2.network.receive ();
    std::unique_ptr <mu_coin::send_block> block (new mu_coin::send_block);
    mu_coin::keypair key1;
    block->inputs.push_back (mu_coin::send_input (key1.pub, 0, 20));
    block->signatures.push_back (mu_coin::uint512_union ());
    mu_coin::sign_message (key1.prv, key1.pub, block->hash (), block->signatures.back ());
    client1.network.publish_block (client2.network.socket.local_endpoint (), std::move (block));
    while (client1.network.publish_unk_count == 0)
    {
        service.run_one ();
    }
    ASSERT_EQ (1, client2.network.publish_req_count);
    ASSERT_EQ (1, client1.network.publish_unk_count);
}

TEST (network, send_valid_publish)
{
    boost::asio::io_service service;
    mu_coin::processor_service processor;
    mu_coin::secret_key secret;
    mu_coin::keypair key1;
    mu_coin::client client1 (service, 24001, processor);
    client1.wallet.insert (key1.pub, key1.prv, secret);
    client1.store.genesis_put (key1.pub, 100);
    mu_coin::keypair key2;
    mu_coin::client client2 (service, 24002, processor);
    client2.wallet.insert (key2.pub, key2.prv, secret);
    client2.store.genesis_put (key1.pub, 100);
    client1.network.receive ();
    client2.network.receive ();
    mu_coin::send_block block2;
    mu_coin::block_hash hash1;
    ASSERT_FALSE (client1.store.latest_get (key1.pub, hash1));
    block2.inputs.push_back (mu_coin::send_input (key1.pub, hash1, 50));
    block2.signatures.push_back (mu_coin::uint512_union ());
    block2.outputs.push_back (mu_coin::send_output (key2.pub, 50));
    auto hash2 (block2.hash ());
    mu_coin::sign_message (key1.prv, key1.pub, hash2, block2.signatures.back ());
    mu_coin::block_hash hash3;
    ASSERT_FALSE (client2.store.latest_get (key1.pub, hash3));
    client1.network.publish_block (client2.network.socket.local_endpoint (), std::unique_ptr <mu_coin::block> (new mu_coin::send_block (block2)));
    while (client2.network.publish_con_count == 0)
    {
        service.run_one ();
    }
    ASSERT_EQ (1, client1.network.publish_con_count);
    ASSERT_EQ (1, client2.network.publish_con_count);
    ASSERT_EQ (1, client1.network.publish_req_count);
    ASSERT_EQ (1, client2.network.publish_req_count);
    mu_coin::block_hash hash4;
    ASSERT_FALSE (client2.store.latest_get (key1.pub, hash4));
    ASSERT_FALSE (hash3 == hash4);
    ASSERT_EQ (hash2, hash4);
    ASSERT_EQ (50, client2.ledger.balance (key1.pub));
}

TEST (receivable_processor, timeout)
{
    boost::asio::io_service io_service;
    mu_coin::processor_service processor;
    mu_coin::client client (io_service, 24001, processor);
    auto receivable (std::make_shared <mu_coin::receivable_processor> (nullptr, client));
    ASSERT_EQ (0, client.network.publish_listener_size ());
    ASSERT_FALSE (receivable->complete);
    ASSERT_EQ (0, processor.size ());
    receivable->advance_timeout ();
    ASSERT_EQ (1, processor.size ());
    receivable->advance_timeout ();
    ASSERT_EQ (2, processor.size ());
}

TEST (receivable_processor, confirm_no_pos)
{
    boost::asio::io_service io_service;
    mu_coin::processor_service processor;
    mu_coin::client client1 (io_service, 24001, processor);
    auto block1 (new mu_coin::send_block ());
    auto receivable (std::make_shared <mu_coin::receivable_processor> (std::unique_ptr <mu_coin::publish_req> {new mu_coin::publish_req {std::unique_ptr <mu_coin::block> {block1}}}, client1));
    receivable->run ();
    ASSERT_EQ (1, client1.network.publish_listener_size ());
    mu_coin::keypair key1;
    mu_coin::publish_con con1 {block1->hash ()};
    mu_coin::authorization auth1;
    auth1.address = key1.pub;
    mu_coin::sign_message (key1.prv, key1.pub, con1.block, auth1.signature);
    con1.authorizations.push_back (auth1);
    mu_coin::byte_write_stream stream;
    con1.serialize (stream);
    ASSERT_LE (stream.size, client1.network.buffer.size ());
    std::copy (stream.data, stream.data + stream.size, client1.network.buffer.begin ());
    client1.network.receive_action (boost::system::error_code {}, stream.size);
    ASSERT_TRUE (receivable->acknowledged.is_zero ());
}

TEST (receivable_processor, confirm_insufficient_pos)
{
    boost::asio::io_service io_service;
    mu_coin::processor_service processor;
    mu_coin::client client1 (io_service, 24001, processor);
    mu_coin::keypair key1;
    client1.ledger.store.genesis_put (key1.pub, 1);
    auto block1 (new mu_coin::send_block ());
    auto receivable (std::make_shared <mu_coin::receivable_processor> (std::unique_ptr <mu_coin::publish_req> {new mu_coin::publish_req {std::unique_ptr <mu_coin::block> {block1}}}, client1));
    receivable->run ();
    ASSERT_EQ (1, client1.network.publish_listener_size ());
    mu_coin::publish_con con1 {block1->hash ()};
    mu_coin::authorization auth1;
    auth1.address = key1.pub;
    mu_coin::sign_message (key1.prv, key1.pub, con1.block, auth1.signature);
    con1.authorizations.push_back (auth1);
    mu_coin::byte_write_stream stream;
    con1.serialize (stream);
    ASSERT_LE (stream.size, client1.network.buffer.size ());
    std::copy (stream.data, stream.data + stream.size, client1.network.buffer.begin ());
    client1.network.receive_action (boost::system::error_code {}, stream.size);
    ASSERT_EQ (1, receivable->acknowledged);
    ASSERT_FALSE (receivable->complete);
    // Shared_from_this, local, timeout, callback
    ASSERT_EQ (4, receivable.use_count ());
}

TEST (receivable_processor, confirm_sufficient_pos)
{
    boost::asio::io_service io_service;
    mu_coin::processor_service processor;
    mu_coin::client client1 (io_service, 24001, processor);
    mu_coin::keypair key1;
    client1.ledger.store.genesis_put (key1.pub, std::numeric_limits<mu_coin::uint256_t>::max ());
    auto block1 (new mu_coin::send_block ());
    auto receivable (std::make_shared <mu_coin::receivable_processor> (std::unique_ptr <mu_coin::publish_req> {new mu_coin::publish_req {std::unique_ptr <mu_coin::block> {block1}}}, client1));
    receivable->run ();
    ASSERT_EQ (1, client1.network.publish_listener_size ());
    mu_coin::publish_con con1 {block1->hash ()};
    mu_coin::authorization auth1;
    auth1.address = key1.pub;
    mu_coin::sign_message (key1.prv, key1.pub, con1.block, auth1.signature);
    con1.authorizations.push_back (auth1);
    mu_coin::byte_write_stream stream;
    con1.serialize (stream);
    ASSERT_LE (stream.size, client1.network.buffer.size ());
    std::copy (stream.data, stream.data + stream.size, client1.network.buffer.begin ());
    client1.network.receive_action (boost::system::error_code {}, stream.size);
    ASSERT_EQ (std::numeric_limits<mu_coin::uint256_t>::max (), receivable->acknowledged);
    ASSERT_TRUE (receivable->complete);
    ASSERT_EQ (3, receivable.use_count ());
}

TEST (receivable_processor, send_with_receive)
{
    boost::asio::io_service io_service;
    mu_coin::processor_service processor;
    mu_coin::client client1 (io_service, 24001, processor);
    mu_coin::client client2 (io_service, 24002, processor);
    mu_coin::keypair key1;
    client1.wallet.insert (key1.pub, key1.prv, 0);
    mu_coin::keypair key2;
    client2.wallet.insert (key2.pub, key2.prv, 0);
    auto amount (std::numeric_limits <mu_coin::uint256_t>::max ());
    client1.ledger.store.genesis_put (key1.pub, amount);
    client2.ledger.store.genesis_put (key1.pub, amount);
    auto block1 (new mu_coin::send_block ());
    mu_coin::block_hash previous;
    ASSERT_FALSE (client1.ledger.store.latest_get (key1.pub, previous));
    block1->inputs.push_back (mu_coin::send_input (key1.pub, previous, amount - 100));
    block1->outputs.push_back (mu_coin::send_output (key2.pub, 100));
    block1->signatures.push_back (mu_coin::uint512_union {});
    mu_coin::sign_message (key1.prv, key1.pub, block1->hash (), block1->signatures.back ());
    ASSERT_EQ (amount, client1.ledger.balance (key1.pub));
    ASSERT_EQ (0, client1.ledger.balance (key2.pub));
    ASSERT_EQ (amount, client2.ledger.balance (key1.pub));
    ASSERT_EQ (0, client2.ledger.balance (key2.pub));
    ASSERT_EQ (mu_coin::process_result::progress, client1.ledger.process (*block1));
    ASSERT_EQ (mu_coin::process_result::progress, client2.ledger.process (*block1));
    ASSERT_EQ (amount - 100, client1.ledger.balance (key1.pub));
    ASSERT_EQ (0, client1.ledger.balance (key2.pub));
    ASSERT_EQ (amount - 100, client2.ledger.balance (key1.pub));
    ASSERT_EQ (0, client2.ledger.balance (key2.pub));
    auto receivable (std::make_shared <mu_coin::receivable_processor> (std::unique_ptr <mu_coin::publish_req> {new mu_coin::publish_req {std::unique_ptr <mu_coin::block> {block1}}}, client2));
    receivable->run ();
    ASSERT_EQ (1, client2.network.publish_listener_size ());
    mu_coin::publish_con con1 {block1->hash ()};
    mu_coin::authorization auth1;
    auth1.address = key1.pub;
    mu_coin::sign_message (key1.prv, key1.pub, con1.block, auth1.signature);
    con1.authorizations.push_back (auth1);
    mu_coin::byte_write_stream stream;
    con1.serialize (stream);
    ASSERT_LE (stream.size, client2.network.buffer.size ());
    std::copy (stream.data, stream.data + stream.size, client2.network.buffer.begin ());
    client2.network.receive_action (boost::system::error_code {}, stream.size);
    ASSERT_EQ (amount - 100, client1.ledger.balance (key1.pub));
    ASSERT_EQ (0, client1.ledger.balance (key2.pub));
    ASSERT_EQ (amount - 100, client2.ledger.balance (key1.pub));
    ASSERT_EQ (100, client2.ledger.balance (key2.pub));
    ASSERT_EQ (amount - 100, receivable->acknowledged);
    ASSERT_TRUE (receivable->complete);
    ASSERT_EQ (3, receivable.use_count ());
}