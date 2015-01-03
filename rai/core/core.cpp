#include <rai/core/core.hpp>

#include <ed25519-donna/ed25519.h>

#include <unordered_set>
#include <memory>
#include <sstream>

#include <boost/log/expressions.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

namespace
{
    bool constexpr ledger_logging ()
    {
        return true;
    }
    bool constexpr ledger_duplicate_logging ()
    {
        return ledger_logging () && false;
    }
    bool constexpr network_logging ()
    {
        return true;
    }
    bool constexpr network_message_logging ()
    {
        return network_logging () && true;
    }
    bool constexpr network_publish_logging ()
    {
        return network_logging () && false;
    }
    bool constexpr network_packet_logging ()
    {
        return network_logging () && false;
    }
    bool constexpr network_keepalive_logging ()
    {
        return network_logging () && false;
    }
    bool constexpr client_lifetime_tracing ()
    {
        return false;
    }
    bool constexpr insufficient_work_logging ()
    {
        return network_logging () && true;
    }
    bool constexpr log_rpc ()
    {
        return network_logging () && true;
    }
    bool constexpr bulk_pull_logging ()
    {
        return network_logging () && true;
    }
    bool constexpr work_generation_time ()
    {
        return true;
    }
    bool constexpr log_to_cerr ()
    {
        return false;
    }
}

rai::message_parser::message_parser (rai::message_visitor & visitor_a) :
visitor (visitor_a),
error (false),
insufficient_work (false)
{
}

void rai::message_parser::deserialize_buffer (uint8_t const * buffer_a, size_t size_a)
{
    error = false;
    rai::bufferstream header_stream (buffer_a, size_a);
    uint8_t version_max;
    uint8_t version_using;
    uint8_t version_min;
    rai::message_type type;
    std::bitset <16> extensions;
    if (!rai::message::read_header (header_stream, version_max, version_using, version_min, type, extensions))
    {
        switch (type)
        {
            case rai::message_type::keepalive:
            {
                deserialize_keepalive (buffer_a, size_a);
                break;
            }
            case rai::message_type::publish:
            {
                deserialize_publish (buffer_a, size_a);
                break;
            }
            case rai::message_type::confirm_req:
            {
                deserialize_confirm_req (buffer_a, size_a);
                break;
            }
            case rai::message_type::confirm_ack:
            {
                deserialize_confirm_ack (buffer_a, size_a);
                break;
            }
            default:
            {
                error = true;
                break;
            }
        }
    }
    else
    {
        error = true;
    }
}

void rai::message_parser::deserialize_keepalive (uint8_t const * buffer_a, size_t size_a)
{
    rai::keepalive incoming;
    rai::bufferstream stream (buffer_a, size_a);
    auto error_l (incoming.deserialize (stream));
    if (!error_l && at_end (stream))
    {
        visitor.keepalive (incoming);
    }
    else
    {
        error = true;
    }
}

void rai::message_parser::deserialize_publish (uint8_t const * buffer_a, size_t size_a)
{
    rai::publish incoming;
    rai::bufferstream stream (buffer_a, size_a);
    auto error_l (incoming.deserialize (stream));
    if (!error_l && at_end (stream))
    {
        if (!rai::work_validate (*incoming.block))
        {
            visitor.publish (incoming);
        }
        else
        {
            insufficient_work = true;
        }
    }
    else
    {
        error = true;
    }
}

void rai::message_parser::deserialize_confirm_req (uint8_t const * buffer_a, size_t size_a)
{
    rai::confirm_req incoming;
    rai::bufferstream stream (buffer_a, size_a);
    auto error_l (incoming.deserialize (stream));
    if (!error_l && at_end (stream))
    {
        if (!rai::work_validate (*incoming.block))
        {
            visitor.confirm_req (incoming);
        }
        else
        {
            insufficient_work = true;
        }
    }
    else
    {
        error = true;
    }
}

void rai::message_parser::deserialize_confirm_ack (uint8_t const * buffer_a, size_t size_a)
{
    rai::confirm_ack incoming;
    rai::bufferstream stream (buffer_a, size_a);
    auto error_l (incoming.deserialize (stream));
    if (!error_l && at_end (stream))
    {
        if (!rai::work_validate (*incoming.vote.block))
        {
            visitor.confirm_ack (incoming);
        }
        else
        {
            insufficient_work = true;
        }
    }
    else
    {
        error = true;
    }
}

bool rai::message_parser::at_end (rai::bufferstream & stream_a)
{
    uint8_t junk;
    auto end (rai::read (stream_a, junk));
    return end;
}

std::chrono::seconds constexpr rai::processor::period;
std::chrono::seconds constexpr rai::processor::cutoff;
std::chrono::milliseconds const rai::confirm_wait = rai_network == rai_networks::rai_test_network ? std::chrono::milliseconds (0) : std::chrono::milliseconds (5000);

rai::network::network (boost::asio::io_service & service_a, uint16_t port, rai::client & client_a) :
socket (service_a, boost::asio::ip::udp::endpoint (boost::asio::ip::address_v6::any (), port)),
service (service_a),
resolver (service_a),
client (client_a),
bad_sender_count (0),
on (true),
keepalive_count (0),
publish_count (0),
confirm_req_count (0),
confirm_ack_count (0),
insufficient_work_count (0),
error_count (0)
{
}

void rai::network::receive ()
{
    if (network_packet_logging ())
    {
        BOOST_LOG (client.log) << "Receiving packet";
    }
    std::unique_lock <std::mutex> lock (socket_mutex);
    socket.async_receive_from (boost::asio::buffer (buffer.data (), buffer.size ()), remote,
        [this] (boost::system::error_code const & error, size_t size_a)
        {
            receive_action (error, size_a);
        });
}

void rai::network::stop ()
{
    on = false;
    socket.close ();
    resolver.cancel ();
}

void rai::network::send_keepalive (rai::endpoint const & endpoint_a)
{
    assert (endpoint_a.address ().is_v6 ());
    rai::keepalive message;
    client.peers.random_fill (message.peers);
    std::shared_ptr <std::vector <uint8_t>> bytes (new std::vector <uint8_t>);
    {
        rai::vectorstream stream (*bytes);
        message.serialize (stream);
    }
    if (network_keepalive_logging ())
    {
        BOOST_LOG (client.log) << boost::str (boost::format ("Keepalive req sent from %1% to %2%") % endpoint () % endpoint_a);
    }
    auto client_l (client.shared ());
    send_buffer (bytes->data (), bytes->size (), endpoint_a, [bytes, client_l, endpoint_a] (boost::system::error_code const & ec, size_t)
        {
            if (network_logging ())
            {
                if (ec)
                {
                    BOOST_LOG (client_l->log) << boost::str (boost::format ("Error sending keepalive from %1% to %2% %3%") % client_l->network.endpoint () % endpoint_a % ec.message ());
                }
            }
        });
}

void rai::network::republish_block (std::unique_ptr <rai::block> block)
{
	auto hash (block->hash ());
    auto list (client.peers.list ());
    if (!confirm_broadcast (list, block->clone(), 0))
    {
        rai::publish message (std::move (block));
        std::shared_ptr <std::vector <uint8_t>> bytes (new std::vector <uint8_t>);
        {
            rai::vectorstream stream (*bytes);
            message.serialize (stream);
        }
        auto client_l (client.shared ());
        for (auto i (list.begin ()), n (list.end ()); i != n; ++i)
        {
			if (!client.peers.knows_about (i->endpoint, hash))
			{
				if (network_publish_logging ())
				{
					BOOST_LOG (client.log) << boost::str (boost::format ("Publish %1% to %2%") % message.block->hash ().to_string () % i->endpoint);
				}
				send_buffer (bytes->data (), bytes->size (), i->endpoint, [bytes, client_l] (boost::system::error_code const & ec, size_t size)
					{
						if (network_logging ())
						{
							if (ec)
							{
								BOOST_LOG (client_l->log) << boost::str (boost::format ("Error sending publish: %1%") % ec.message ());
							}
						}
					});
			}
        }
    }
}

void rai::network::send_confirm_req (boost::asio::ip::udp::endpoint const & endpoint_a, rai::block const & block)
{
    rai::confirm_req message (block.clone ());
    std::shared_ptr <std::vector <uint8_t>> bytes (new std::vector <uint8_t>);
    {
        rai::vectorstream stream (*bytes);
        message.serialize (stream);
    }
    if (network_logging ())
    {
        BOOST_LOG (client.log) << boost::str (boost::format ("Sending confirm req to %1%") % endpoint_a);
    }
    auto client_l (client.shared ());
    send_buffer (bytes->data (), bytes->size (), endpoint_a, [bytes, client_l] (boost::system::error_code const & ec, size_t size)
        {
            if (network_logging ())
            {
                if (ec)
                {
                    BOOST_LOG (client_l->log) << boost::str (boost::format ("Error sending confirm request: %1%") % ec.message ());
                }
            }
        });
}

namespace
{
class network_message_visitor : public rai::message_visitor
{
public:
    network_message_visitor (rai::client & client_a, rai::endpoint const & sender_a) :
    client (client_a),
    sender (sender_a)
    {
    }
    void keepalive (rai::keepalive const & message_a) override
    {
        if (network_keepalive_logging ())
        {
            BOOST_LOG (client.log) << boost::str (boost::format ("Received keepalive from %1%") % sender);
        }
        ++client.network.keepalive_count;
        client.processor.contacted (sender);
        client.network.merge_peers (message_a.peers);
    }
    void publish (rai::publish const & message_a) override
    {
        if (network_message_logging ())
        {
            BOOST_LOG (client.log) << boost::str (boost::format ("Received publish req from %1%") % sender);
        }
        ++client.network.publish_count;
        client.processor.contacted (sender);
        client.peers.insert (sender, message_a.block->hash ());
        client.processor.process_receive_republish (message_a.block->clone ());
    }
    void confirm_req (rai::confirm_req const & message_a) override
    {
        if (network_message_logging ())
        {
            BOOST_LOG (client.log) << boost::str (boost::format ("Received confirm req %1%") % sender);
        }
        ++client.network.confirm_req_count;
        client.processor.contacted (sender);
        client.peers.insert (sender, message_a.block->hash ());
        client.processor.process_receive_republish (message_a.block->clone ());
        if (client.store.block_exists (message_a.block->hash ()))
        {
            client.processor.process_confirmation (*message_a.block, sender);
        }
    }
    void confirm_ack (rai::confirm_ack const & message_a) override
    {
        if (network_message_logging ())
        {
            BOOST_LOG (client.log) << boost::str (boost::format ("Received Confirm from %1%") % sender);
        }
        ++client.network.confirm_ack_count;
        client.processor.contacted (sender);
        client.peers.insert (sender, message_a.vote.block->hash ());
        client.processor.process_receive_republish (message_a.vote.block->clone ());
        client.vote (message_a.vote);
    }
    void bulk_pull (rai::bulk_pull const &) override
    {
        assert (false);
    }
    void bulk_push (rai::bulk_push const &) override
    {
        assert (false);
    }
    void frontier_req (rai::frontier_req const &) override
    {
        assert (false);
    }
    rai::client & client;
    rai::endpoint sender;
};
}

void rai::network::receive_action (boost::system::error_code const & error, size_t size_a)
{
    if (!error && on)
    {
        if (!rai::reserved_address (remote) && remote != endpoint ())
        {
            network_message_visitor visitor (client, remote);
            rai::message_parser parser (visitor);
            parser.deserialize_buffer (buffer.data (), size_a);
            if (parser.error)
            {
                ++error_count;
            }
            else if (parser.insufficient_work)
            {
                ++insufficient_work_count;
            }
        }
        else
        {
            if (network_logging ())
            {
                BOOST_LOG (client.log) << "Reserved sender";
            }
            ++bad_sender_count;
        }
        receive ();
    }
    else
    {
        if (network_logging ())
        {
            BOOST_LOG (client.log) << boost::str (boost::format ("Receive error: %1%") % error.message ());
        }
        client.service.add (std::chrono::system_clock::now () + std::chrono::seconds (5), [this] () { receive (); });
    }
}

// Send keepalives to all the peers we've been notified of
void rai::network::merge_peers (std::array <rai::endpoint, 8> const & peers_a)
{
    for (auto i (peers_a.begin ()), j (peers_a.end ()); i != j; ++i)
    {
        if (!client.peers.not_a_peer (*i) && !client.peers.known_peer (*i))
        {
            send_keepalive (*i);
        }
    }
}

rai::publish::publish () :
message (rai::message_type::publish)
{
}

rai::publish::publish (std::unique_ptr <rai::block> block_a) :
message (rai::message_type::publish),
block (std::move (block_a))
{
    block_type_set (block->type ());
}

bool rai::publish::deserialize (rai::stream & stream_a)
{
    auto result (read_header (stream_a, version_max, version_using, version_min, type, extensions));
    assert (!result);
	assert (type == rai::message_type::publish);
    if (!result)
    {
        block = rai::deserialize_block (stream_a, block_type ());
        result = block == nullptr;
    }
    return result;
}

void rai::publish::serialize (rai::stream & stream_a)
{
    assert (block != nullptr);
	write_header (stream_a);
    block->serialize (stream_a);
}

rai::uint256_union const rai::wallet_store::version_1 (1);
rai::uint256_union const rai::wallet_store::version_current (version_1);
rai::uint256_union const rai::wallet_store::version_special (0);
rai::uint256_union const rai::wallet_store::salt_special (1);
rai::uint256_union const rai::wallet_store::wallet_key_special (2);
rai::uint256_union const rai::wallet_store::check_special (3);
rai::uint256_union const rai::wallet_store::representative_special (4);
int const rai::wallet_store::special_count (5);

rai::wallet_store::wallet_store (bool & init_a, boost::filesystem::path const & path_a, std::string const & json_a) :
password (0, 1024)
{
    init_a = false;
    initialize (init_a, path_a);
    if (!init_a)
    {
        std::string junk;
        assert (handle->Get (leveldb::ReadOptions (), leveldb::Slice (version_special.chars.data (), version_special.chars.size ()), &junk).IsNotFound ());
        boost::property_tree::ptree wallet_l;
        std::stringstream istream (json_a);
        boost::property_tree::read_json (istream, wallet_l);
        for (auto i (wallet_l.begin ()), n (wallet_l.end ()); i != n; ++i)
        {
            rai::uint256_union key;
            init_a = key.decode_hex (i->first);
            if (!init_a)
            {
                rai::uint256_union value;
                init_a = value.decode_hex (wallet_l.get <std::string> (i->first));
                if (!init_a)
                {
                    auto status (handle->Put (leveldb::WriteOptions (), leveldb::Slice (key.chars.data (), key.chars.size ()), leveldb::Slice (value.chars.data (), value.chars.size ())));
                    if (!status.ok ())
                    {
                        init_a = true;
                    }
                }
                else
                {
                    init_a = true;
                }
            }
            else
            {
                init_a = true;
            }
        }
        if (!init_a)
        {
            auto status0 (handle->Get (leveldb::ReadOptions (), leveldb::Slice (version_special.chars.data (), version_special.chars.size ()), &junk));
            if (status0.ok ())
            {
                auto status1 (handle->Get (leveldb::ReadOptions (), leveldb::Slice (wallet_key_special.chars.data (), wallet_key_special.chars.size ()), &junk));
                if (status1.ok ())
                {
                    auto status2 (handle->Get (leveldb::ReadOptions (), leveldb::Slice (salt_special.chars.data (), salt_special.chars.size ()), &junk));
                    if (status2.ok ())
                    {
                        auto status3 (handle->Get (leveldb::ReadOptions (), leveldb::Slice (check_special.chars.data (), check_special.chars.size ()), &junk));
                        if (status3.ok ())
                        {
                            auto status4 (handle->Get (leveldb::ReadOptions (), leveldb::Slice (representative_special.chars.data (), representative_special.chars.size ()), &junk));
                            if (status4.ok ())
                            {
                                enter_password ("");
                            }
                            else
                            {
                                init_a = true;
                            }
                        }
                        else
                        {
                            init_a = true;
                        }
                    }
                    else
                    {
                        init_a = true;
                    }
                }
                else
                {
                    init_a = true;
                }
            }
            else
            {
                init_a = true;
            }
        }
    }
}

rai::wallet_store::wallet_store (bool & init_a, boost::filesystem::path const & path_a) :
password (0, 1024)
{
    init_a = false;
    initialize (init_a, path_a);
    if (!init_a)
    {
        std::string version_value;
        auto version_status (handle->Get (leveldb::ReadOptions (), leveldb::Slice (version_special.chars.data (), version_special.chars.size ()), &version_value));
        if (version_status.IsNotFound ())
        {
            auto status0 (handle->Put (leveldb::WriteOptions (), leveldb::Slice (version_special.chars.data (), version_special.chars.size ()), leveldb::Slice (version_current.chars.data (), version_current.chars.size ())));
            assert (status0.ok ());
            rai::uint256_union salt_l;
            random_pool.GenerateBlock (salt_l.bytes.data (), salt_l.bytes.size ());
            auto status2 (handle->Put (leveldb::WriteOptions (), leveldb::Slice (salt_special.chars.data (), salt_special.chars.size ()), leveldb::Slice (salt_l.chars.data (), salt_l.chars.size ())));
            assert (status2.ok ());
            // Wallet key is a fixed random key that encrypts all entries
            rai::uint256_union wallet_key;
            random_pool.GenerateBlock (wallet_key.bytes.data (), sizeof (wallet_key.bytes));
            auto password_l (derive_key (""));
            password.value_set (password_l);
            // Wallet key is encrypted by the user's password
            rai::uint256_union encrypted (wallet_key, password_l, salt_l.owords [0]);
            auto status1 (handle->Put (leveldb::WriteOptions (), leveldb::Slice (wallet_key_special.chars.data (), wallet_key_special.chars.size ()), leveldb::Slice (encrypted.chars.data (), encrypted.chars.size ())));
            assert (status1.ok ());
            rai::uint256_union zero (0);
            rai::uint256_union check (zero, wallet_key, salt_l.owords [0]);
            auto status3 (handle->Put (leveldb::WriteOptions (), leveldb::Slice (check_special.chars.data (), check_special.chars.size ()), leveldb::Slice (check.chars.data (), check.chars.size ())));
            assert (status3.ok ());
            wallet_key.clear ();
            password_l.clear ();
            auto status4 (handle->Put (leveldb::WriteOptions (), leveldb::Slice (representative_special.chars.data (), representative_special.chars.size ()), leveldb::Slice (rai::genesis_account.chars.data (), rai::genesis_account.chars.size ())));
            assert (status4.ok ());
        }
        else
        {
            enter_password ("");
        }
    }
}

void rai::wallet_store::initialize (bool & init_a, boost::filesystem::path const & path_a)
{
    boost::system::error_code code;
    boost::filesystem::create_directories (path_a, code);
    if (!code)
    {
        leveldb::Options options;
        options.create_if_missing = true;
        leveldb::DB * db (nullptr);
        auto status (leveldb::DB::Open (options, path_a.string (), &db));
        handle.reset (db);
        if (!status.ok ())
        {
            init_a = true;
        }
    }
    else
    {
        init_a = true;
    }
}

bool rai::wallet_store::is_representative ()
{
    return exists (representative ());
}

void rai::wallet_store::representative_set (rai::account const & representative_a)
{
    auto status (handle->Put (leveldb::WriteOptions (), leveldb::Slice (representative_special.chars.data (), representative_special.chars.size ()), leveldb::Slice (representative_a.chars.data (), representative_a.chars.size ())));
    assert (status.ok ());
}

rai::account rai::wallet_store::representative ()
{
    std::string representative_l;
    auto status (handle->Get (leveldb::ReadOptions (), leveldb::Slice (representative_special.chars.data (), representative_special.chars.size ()), &representative_l));
    assert (status.ok ());
    rai::account result;
    assert (representative_l.size () == result.chars.size ());
    std::copy (representative_l.begin (), representative_l.end (), result.chars.begin ());
    return result;
}

void rai::wallet_store::insert (rai::private_key const & prv)
{
    rai::public_key pub;
    ed25519_publickey (prv.bytes.data (), pub.bytes.data ());
    rai::uint256_union encrypted (prv, wallet_key (), salt ().owords [0]);
    auto status (handle->Put (leveldb::WriteOptions (), leveldb::Slice (pub.chars.data (), pub.chars.size ()), leveldb::Slice (encrypted.chars.data (), encrypted.chars.size ())));
    assert (status.ok ());
}

void rai::wallet_store::erase (rai::public_key const & pub)
{
    auto status (handle->Delete (leveldb::WriteOptions (), leveldb::Slice (pub.chars.data (), pub.chars.size ())));
    assert (status.ok ());
}

bool rai::wallet_store::fetch (rai::public_key const & pub, rai::private_key & prv)
{
    auto result (false);
    std::string value;
    auto status (handle->Get (leveldb::ReadOptions (), leveldb::Slice (pub.chars.data (), pub.chars.size ()), &value));
    if (status.ok ())
    {
        rai::uint256_union encrypted;
        rai::bufferstream stream (reinterpret_cast <uint8_t const *> (value.data ()), value.size ());
        auto result2 (read (stream, encrypted.bytes));
        assert (!result2);
        prv = encrypted.prv (wallet_key (), salt ().owords [0]);
        rai::public_key compare;
        ed25519_publickey (prv.bytes.data (), compare.bytes.data ());
        if (!(pub == compare))
        {
            result = true;
        }
    }
    else
    {
        result = true;
    }
    return result;
}

bool rai::wallet_store::exists (rai::public_key const & pub)
{
    return find (pub) != end ();
}

void rai::wallet_store::serialize_json (std::string & string_a)
{
    boost::property_tree::ptree tree;
    std::unique_ptr <leveldb::Iterator> iterator (handle->NewIterator (leveldb::ReadOptions ()));
    iterator->SeekToFirst ();
    for (; iterator->Valid (); iterator->Next ())
    {
        rai::uint256_union key;
        key = iterator->key ();
        rai::uint256_union value;
        value = iterator->value ();
        std::string key_hex;
        key.encode_hex (key_hex);
        std::string value_hex;
        value.encode_hex (value_hex);
        tree.put (key_hex, value_hex);
    }
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, tree);
    string_a = ostream.str ();
}

bool rai::wallet_store::move (rai::wallet_store & other_a, std::vector <rai::public_key> const & keys)
{
    assert (valid_password ());
    assert (other_a.valid_password ());
    auto result (false);
    for (auto i (keys.begin ()), n (keys.end ()); i != n; ++i)
    {
        rai::private_key prv;
        auto error (other_a.fetch (*i, prv));
        result = result | error;
        if (!result)
        {
            insert (prv);
            other_a.erase (*i);
        }
    }
    return result;
}

rai::wallet::wallet (bool & init_a, rai::client & client_a, boost::filesystem::path const & path_a) :
store (init_a, path_a),
client (client_a)
{
}

rai::wallet::wallet (bool & init_a, rai::client & client_a, boost::filesystem::path const & path_a, std::string const & json) :
store (init_a, path_a, json),
client (client_a)
{
}

bool rai::wallet::receive (rai::send_block const & send_a, rai::private_key const & prv_a, rai::account const & representative_a)
{
    std::lock_guard <std::mutex> lock (mutex);
    auto hash (send_a.hash ());
    bool result;
    if (client.ledger.store.pending_exists (hash))
    {
        rai::frontier frontier;
        auto new_account (client.ledger.store.latest_get (send_a.hashables.destination, frontier));
        std::unique_ptr <rai::block> block;
        if (new_account)
        {
            auto open (new rai::open_block);
            open->hashables.source = hash;
            open->hashables.representative = representative_a;
            client.work_create (*open);
            rai::sign_message (prv_a, send_a.hashables.destination, open->hash (), open->signature);
            block.reset (open);
        }
        else
        {
            auto receive (new rai::receive_block);
            receive->hashables.previous = frontier.hash;
            receive->hashables.source = hash;
            client.work_create (*receive);
            rai::sign_message (prv_a, send_a.hashables.destination, receive->hash (), receive->signature);
            block.reset (receive);
        }
        client.processor.process_receive_republish (std::move (block));
        result = false;
    }
    else
    {
        result = true;
        // Ledger doesn't have this marked as available to receive anymore
    }
    return result;
}

bool rai::wallet::send (rai::account const & account_a, rai::uint128_t const & amount_a)
{
    std::lock_guard <std::mutex> lock (mutex);
    std::vector <std::unique_ptr <rai::send_block>> blocks;
    auto result (!store.valid_password ());
    if (!result)
    {
        rai::uint128_t remaining (amount_a);
        for (auto i (store.begin ()), j (store.end ()); i != j && !result && !remaining.is_zero (); ++i)
        {
            auto account (i->first);
            auto balance (client.ledger.account_balance (account));
            if (!balance.is_zero ())
            {
                rai::frontier frontier;
                result = client.ledger.store.latest_get (account, frontier);
                assert (!result);
                auto amount (std::min (remaining, balance));
                remaining -= amount;
                std::unique_ptr <rai::send_block> block (new rai::send_block);
                block->hashables.destination = account_a;
                block->hashables.previous = frontier.hash;
                block->hashables.balance = balance - amount;
                client.work_create (*block);
                rai::private_key prv;
                result = store.fetch (account, prv);
                assert (!result);
                sign_message (prv, account, block->hash (), block->signature);
                prv.clear ();
                blocks.push_back (std::move (block));
            }
        }
        if (!remaining.is_zero ())
        {
            BOOST_LOG (client.log) << "Wallet contained insufficient coins";
            // Destroy the sends because they're signed and we're not going to use them.
            result = true;
            blocks.clear ();
        }
        else
        {
            BOOST_LOG (client.log) << "Publishing blocks";
            for (auto i (blocks.begin ()), j (blocks.end ()); i != j; ++i)
            {
                client.processor.process_receive_republish (std::move (*i));
            }
        }
    }
    else
    {
        BOOST_LOG (client.log) << "Wallet key is invalid";
    }
    return result;
}

bool rai::wallet::import (std::string const & json_a, std::string const & password_a)
{
    std::lock_guard <std::mutex> lock (mutex);
    auto result (!store.valid_password ());
    rai::wallet_store store_l (result, boost::filesystem::unique_path (), json_a);
    if (!result)
    {
        store_l.enter_password (password_a);
        result = !store_l.valid_password ();
        if (!result)
        {
            std::vector <rai::public_key> accounts;
            for (auto i (store_l.begin ()), n (store_l.end ()); i != n; ++i)
            {
                accounts.push_back (i->first);
            }
            result = store.move (store_l, accounts);
        }
    }
    return result;
}

rai::wallets::wallets (rai::client & client_a, boost::filesystem::path const & path_a) :
path (path_a),
client (client_a)
{
    boost::filesystem::create_directories (path_a);
    boost::filesystem::directory_iterator i (path_a);
    boost::filesystem::directory_iterator n;
    for (; i != n; ++i)
    {
        if (boost::filesystem::is_directory (i->path ()))
        {
            rai::uint256_union id;
            if (!id.decode_hex (i->path ().filename ().string ()))
            {
                assert (items.find (id) == items.end ());
                auto error (false);
                auto wallet (std::make_shared <rai::wallet> (error, client, i->path ()));
                if (!error)
                {
                    items [id] = wallet;
                }
                else
                {
                    // Couldn't open wallet
                }
            }
            else
            {
                // Non-id directory in wallets directory
            }
        }
        else
        {
            // Non-directory in wallets directory
        }
    }
}

std::shared_ptr <rai::wallet> rai::wallets::open (rai::uint256_union const & id_a)
{
    std::shared_ptr <rai::wallet> result;
    auto existing (items.find (id_a));
    if (existing != items.end ())
    {
        result = existing->second;
    }
    return result;
}

std::shared_ptr <rai::wallet> rai::wallets::create (rai::uint256_union const & id_a)
{
    assert (items.find (id_a) == items.end ());
    std::shared_ptr <rai::wallet> result;
    bool error;
    std::string id;
    id_a.encode_hex (id);
    auto wallet (std::make_shared <rai::wallet> (error, client, path / id));
    if (!error)
    {
        items [id_a] = wallet;
        result = wallet;
    }
    return result;
}

void rai::wallets::destroy (rai::uint256_union const & id_a)
{
    auto existing (items.find (id_a));
    assert (existing != items.end ());
    auto wallet (existing->second);
    items.erase (existing);
    std::lock_guard <std::mutex> lock (wallet->mutex);
    wallet->store.handle.reset ();
    assert (boost::filesystem::is_directory (path / id_a.to_string ()));
    boost::filesystem::remove_all (path / id_a.to_string ());
}

rai::key_iterator::key_iterator (leveldb::DB * db_a) :
iterator (db_a->NewIterator (leveldb::ReadOptions ()))
{
    iterator->SeekToFirst ();
    set_current ();
}

rai::key_iterator::key_iterator (leveldb::DB * db_a, std::nullptr_t) :
iterator (db_a->NewIterator (leveldb::ReadOptions ()))
{
    set_current ();
}

rai::key_iterator::key_iterator (leveldb::DB * db_a, rai::uint256_union const & key_a) :
iterator (db_a->NewIterator (leveldb::ReadOptions ()))
{
    iterator->Seek (leveldb::Slice (key_a.chars.data (), key_a.chars.size ()));
    set_current ();
}

void rai::key_iterator::set_current ()
{
    if (iterator->Valid ())
    {
        current.first = iterator->key ();
        current.second = iterator->value ();
    }
    else
    {
        current.first.clear ();
        current.second.clear ();
    }
}

rai::key_iterator & rai::key_iterator::operator ++ ()
{
    iterator->Next ();
    set_current ();
    return *this;
}

rai::key_entry & rai::key_iterator::operator -> ()
{
    return current;
}

rai::key_iterator rai::wallet_store::begin ()
{
    rai::key_iterator result (handle.get ());
    for (auto i (0); i < special_count; ++i)
    {
        assert (result != end ());
        ++result;
    }
    return result;
}

rai::key_iterator rai::wallet_store::find (rai::uint256_union const & key)
{
    rai::key_iterator result (handle.get (), key);
    rai::key_iterator end (handle.get (), nullptr);
    if (result != end)
    {
        if (result.current.first == key)
        {
            return result;
        }
        else
        {
            return end;
        }
    }
    else
    {
        return end;
    }
}

rai::key_iterator rai::wallet_store::end ()
{
    return rai::key_iterator (handle.get (), nullptr);
}

bool rai::key_iterator::operator == (rai::key_iterator const & other_a) const
{
    auto lhs_valid (iterator->Valid ());
    auto rhs_valid (other_a.iterator->Valid ());
    return (!lhs_valid && !rhs_valid) || (lhs_valid && rhs_valid && current.first == other_a.current.first);
}

bool rai::key_iterator::operator != (rai::key_iterator const & other_a) const
{
    return !(*this == other_a);
}

rai::key_iterator & rai::key_iterator::operator = (rai::key_iterator && other_a)
{
    iterator = std::move (other_a.iterator);
    current = other_a.current;
    return *this;
}

void rai::processor_service::run ()
{
    std::unique_lock <std::mutex> lock (mutex);
    while (!done)
    {
        if (!operations.empty ())
        {
            auto & operation_l (operations.top ());
            if (operation_l.wakeup < std::chrono::system_clock::now ())
            {
                auto operation (operation_l);
                operations.pop ();
                lock.unlock ();
                operation.function ();
                lock.lock ();
            }
            else
            {
                condition.wait_until (lock, operation_l.wakeup);
            }
        }
        else
        {
            condition.wait (lock);
        }
    }
}

size_t rai::processor_service::poll_one ()
{
    std::unique_lock <std::mutex> lock (mutex);
    size_t result (0);
    if (!operations.empty ())
    {
        auto & operation_l (operations.top ());
        if (operation_l.wakeup < std::chrono::system_clock::now ())
        {
            auto operation (operation_l);
            operations.pop ();
            lock.unlock ();
            operation.function ();
            result = 1;
        }
    }
    return result;
}

size_t rai::processor_service::poll ()
{
    std::unique_lock <std::mutex> lock (mutex);
    size_t result (0);
    auto done_l (false);
    while (!done_l)
    {
        if (!operations.empty ())
        {
            auto & operation_l (operations.top ());
            if (operation_l.wakeup < std::chrono::system_clock::now ())
            {
                auto operation (operation_l);
                operations.pop ();
                lock.unlock ();
                operation.function ();
                ++result;
                lock.lock ();
            }
            else
            {
                done_l = true;
            }
        }
        else
        {
            done_l = true;
        }
    }
    return result;
}

void rai::processor_service::add (std::chrono::system_clock::time_point const & wakeup_a, std::function <void ()> const & operation)
{
    std::lock_guard <std::mutex> lock (mutex);
    if (!done)
    {
        operations.push (rai::operation ({wakeup_a, operation}));
        condition.notify_all ();
    }
}

rai::processor_service::processor_service () :
done (false)
{
}

void rai::processor_service::stop ()
{
    std::lock_guard <std::mutex> lock (mutex);
    done = true;
    while (!operations.empty ())
    {
        operations.pop ();
    }
    condition.notify_all ();
}

rai::processor::processor (rai::client & client_a) :
client (client_a)
{
}

// We were contacted by `endpoint_a', update peers
void rai::processor::contacted (rai::endpoint const & endpoint_a)
{
    auto endpoint_l (endpoint_a);
    if (endpoint_l.address ().is_v4 ())
    {
        endpoint_l = rai::endpoint (boost::asio::ip::address_v6::v4_mapped (endpoint_l.address ().to_v4 ()), endpoint_l.port ());
    }
    assert (endpoint_l.address ().is_v6 ());
	client.peers.insert (endpoint_l);
}

void rai::processor::stop ()
{
}

bool rai::operation::operator > (rai::operation const & other_a) const
{
    return wakeup > other_a.wakeup;
}

rai::client_init::client_init () :
wallet_init (false)
{
}

bool rai::client_init::error ()
{
    return !block_store_init.ok () || wallet_init || ledger_init;
}

rai::client::client (rai::client_init & init_a, boost::shared_ptr <boost::asio::io_service> service_a, uint16_t port_a, boost::filesystem::path const & application_path_a, rai::processor_service & processor_a) :
store (init_a.block_store_init, application_path_a / "data"),
gap_cache (*this),
ledger (init_a.ledger_init, init_a.block_store_init, store),
conflicts (*this),
wallets (*this, application_path_a / "wallets"),
network (*service_a, port_a, *this),
bootstrap_initiator (*this),
bootstrap (*service_a, port_a, *this),
processor (*this),
peers (network.endpoint ()),
service (processor_a)
{
	peers.peer_observer = [this] (rai::endpoint const & endpoint_a)
	{
		network.send_keepalive (endpoint_a);
		bootstrap_initiator.warmup (endpoint_a);
	};
    vote_observers.push_back ([this] (rai::vote const & vote_a)
    {
        conflicts.update (vote_a);
    });
    vote_observers.push_back ([this] (rai::vote const & vote_a)
    {
        gap_cache.vote (vote_a);
    });
    if (wallets.items.empty ())
    {
        rai::uint256_union id;
        rai::random_pool.GenerateBlock (id.bytes.data (), id.bytes.size ());
        wallets.create (id);
    }
    if (log_to_cerr ())
    {
        boost::log::add_console_log (std::cerr);
    }
    boost::log::add_common_attributes ();
    boost::log::add_file_log (boost::log::keywords::target = application_path_a / "log", boost::log::keywords::file_name = application_path_a / "log" / "log_%Y-%m-%d_%H-%M-%S.%N.log", boost::log::keywords::rotation_size = 4 * 1024 * 1024, boost::log::keywords::auto_flush = rai::rai_network != rai::rai_networks::rai_test_network, boost::log::keywords::scan_method = boost::log::sinks::file::scan_method::scan_matching, boost::log::keywords::max_size = 16 * 1024 * 1024, boost::log::keywords::format = "[%TimeStamp%]: %Message%");
    BOOST_LOG (log) << "Client starting, version: " << RAIBLOCKS_VERSION_MAJOR << "." << RAIBLOCKS_VERSION_MINOR << "." << RAIBLOCKS_VERSION_PATCH;
    ledger.send_observer = [this] (rai::send_block const & block_a, rai::account const & account_a, rai::amount const & balance_a)
    {
        for (auto & i: send_observers)
        {
            i (block_a, account_a, balance_a);
        }
    };
    ledger.receive_observer = [this] (rai::receive_block const & block_a, rai::account const & account_a, rai::amount const & balance_a)
    {
        for (auto & i: receive_observers)
        {
            i (block_a, account_a, balance_a);
        }
    };
    ledger.open_observer = [this] (rai::open_block const & block_a, rai::account const & account_a, rai::amount const & balance_a, rai::account const & representative_a)
    {
        for (auto & i: open_observers)
        {
            i (block_a, account_a, balance_a, representative_a);
        }
    };
    ledger.change_observer = [this] (rai::change_block const & block_a, rai::account const & account_a, rai::account const & representative_a)
    {
        for (auto & i: change_observers)
        {
            i (block_a, account_a, representative_a);
        }
    };
    send_observers.push_back ([this] (rai::send_block const & block_a, rai::account const & account_a, rai::amount const & balance_a)
    {
        for (auto i (wallets.items.begin ()), n (wallets.items.end ()); i != n; ++i)
        {
            auto & wallet (*i->second);
            if (wallet.store.find (block_a.hashables.destination) != wallet.store.end ())
            {
                if (ledger_logging ())
                {
                    BOOST_LOG (log) << boost::str (boost::format ("Starting fast confirmation of block: %1%") % block_a.hash ().to_string ());
                }
                conflicts.start (block_a, false);
                auto root (block_a.root ());
                std::shared_ptr <rai::block> block_l (block_a.clone ().release ());
                service.add (std::chrono::system_clock::now () + rai::confirm_wait, [this, root, block_l] ()
                {
                    if (conflicts.no_conflict (root))
                    {
                        processor.process_confirmed (*block_l);
                    }
                    else
                    {
                        if (ledger_logging ())
                        {
                            BOOST_LOG (log) << boost::str (boost::format ("Unable to fast-confirm block: %1% because root: %2% is in conflict") % block_l->hash ().to_string () % root.to_string ());
                        }
                    }
                });
            }
        }
    });
    if (!init_a.error ())
    {
        if (client_lifetime_tracing ())
        {
            std::cerr << "Constructing client\n";
        }
        if (store.latest_begin () == store.latest_end ())
        {
            // Store was empty meaning we just created it, add the genesis block
            rai::genesis genesis;
            genesis.initialize (store);
        }
    }
}

rai::client::client (rai::client_init & init_a, boost::shared_ptr <boost::asio::io_service> service_a, uint16_t port_a, rai::processor_service & processor_a) :
client (init_a, service_a, port_a, boost::filesystem::unique_path (), processor_a)
{
}

rai::client::~client ()
{
    if (client_lifetime_tracing ())
    {
        std::cerr << "Destructing client\n";
    }
}

void rai::client::send_keepalive (rai::endpoint const & endpoint_a)
{
    auto endpoint_l (endpoint_a);
    if (endpoint_l.address ().is_v4 ())
    {
        endpoint_l = rai::endpoint (boost::asio::ip::address_v6::v4_mapped (endpoint_l.address ().to_v4 ()), endpoint_l.port ());
    }
    assert (endpoint_l.address ().is_v6 ());
    network.send_keepalive (endpoint_l);
}

void rai::client::work_create (rai::block & block_a)
{
    auto begin (std::chrono::system_clock::now ());
	if (work_generation_time ())
	{
		BOOST_LOG (log) << "Beginning work generation";
	}
    rai::work_generate (block_a);
	if (work_generation_time ())
	{
		BOOST_LOG (log) << "Work generation complete: " << (std::chrono::duration_cast <std::chrono::microseconds> (std::chrono::system_clock::now () - begin).count ()) << "us";
	}
}

void rai::client::vote (rai::vote const & vote_a)
{
    for (auto & i: vote_observers)
    {
        i (vote_a);
    }
}

rai::gap_cache::gap_cache (rai::client & client_a) :
client (client_a)
{
}

void rai::gap_cache::add (rai::block const & block_a, rai::block_hash needed_a)
{
    std::lock_guard <std::mutex> lock (mutex);
    auto existing (blocks.find (needed_a));
    if (existing != blocks.end ())
    {
        blocks.modify (existing, [] (rai::gap_information & info) {info.arrival = std::chrono::system_clock::now ();});
    }
    else
    {
        auto hash (block_a.hash ());
		blocks.insert ({std::chrono::system_clock::now (), needed_a, hash, std::unique_ptr <rai::votes> (new rai::votes (hash)), block_a.clone ()});
        if (blocks.size () > max)
        {
            blocks.get <1> ().erase (blocks.get <1> ().begin ());
        }
    }
}

std::unique_ptr <rai::block> rai::gap_cache::get (rai::block_hash const & hash_a)
{
    std::lock_guard <std::mutex> lock (mutex);
    std::unique_ptr <rai::block> result;
    auto existing (blocks.find (hash_a));
    if (existing != blocks.end ())
    {
        blocks.modify (existing, [&] (rai::gap_information & info) {result.swap (info.block);});
        blocks.erase (existing);
    }
    return result;
}

void rai::gap_cache::vote (rai::vote const & vote_a)
{
    std::lock_guard <std::mutex> lock (mutex);
    auto hash (vote_a.block->hash ());
    auto existing (blocks.get <2> ().find (hash));
    if (existing != blocks.get <2> ().end ())
    {
        auto changed (existing->votes->vote (vote_a));
        if (changed)
        {
            auto winner (client.ledger.winner (*existing->votes));
            if (winner.first > bootstrap_threshold ())
            {
                BOOST_LOG (client.log) << boost::str (boost::format ("Initiating bootstrap for confirmed gap: %1%") % hash.to_string ());
                client.bootstrap_initiator.bootstrap_any ();
            }
        }
    }
}

rai::uint128_t rai::gap_cache::bootstrap_threshold ()
{
    return client.ledger.supply () / 16;
}

bool rai::network::confirm_broadcast (std::vector <rai::peer_information> & list_a, std::unique_ptr <rai::block> block_a, uint64_t sequence_a)
{
    bool result (false);
    for (auto i (client.wallets.items.begin ()), n (client.wallets.items.end ()); i != n; ++i)
    {
        auto & wallet (*i->second);
        if (wallet.store.is_representative ())
        {
            auto pub (wallet.store.representative ());
            rai::private_key prv;
            auto error (wallet.store.fetch (pub, prv));
            if (!error)
            {
                auto hash (block_a->hash ());
                for (auto j (list_a.begin ()), m (list_a.end ()); j != m; ++j)
                {
                    if (!client.peers.knows_about (j->endpoint, hash))
                    {
                        confirm_block (prv, pub, block_a->clone (), sequence_a, j->endpoint);
                    }
                }
            }
            else
            {
                // Wallet is locked
            }
            prv.clear ();
            result = true;
        }
    }
    return result;
}

void rai::network::confirm_block (rai::private_key const & prv, rai::public_key const & pub, std::unique_ptr <rai::block> block_a, uint64_t sequence_a, rai::endpoint const & endpoint_a)
{
    rai::confirm_ack confirm (std::move (block_a));
    confirm.vote.account = pub;
    confirm.vote.sequence = sequence_a;
    rai::sign_message (prv, pub, confirm.vote.hash (), confirm.vote.signature);
    std::shared_ptr <std::vector <uint8_t>> bytes (new std::vector <uint8_t>);
    {
        rai::vectorstream stream (*bytes);
        confirm.serialize (stream);
    }
    if (network_publish_logging ())
    {
        BOOST_LOG (client.log) << boost::str (boost::format ("Confirm %1% to %2%") % confirm.vote.block->hash ().to_string () % endpoint_a);
    }
    auto client_l (client.shared ());
    client.network.send_buffer (bytes->data (), bytes->size (), endpoint_a, [bytes, client_l] (boost::system::error_code const & ec, size_t size_a)
        {
            if (network_logging ())
            {
                if (ec)
                {
                    BOOST_LOG (client_l->log) << boost::str (boost::format ("Error broadcasting confirmation: %1%") % ec.message ());
                }
            }
        });
}

void rai::processor::process_receive_republish (std::unique_ptr <rai::block> incoming)
{
    std::unique_ptr <rai::block> block (std::move (incoming));
    do
    {
        auto hash (block->hash ());
        auto process_result (process_receive (*block));
        switch (process_result)
        {
            case rai::process_result::progress:
            {
                client.network.republish_block (std::move (block));
                break;
            }
            default:
            {
                break;
            }
        }
        block = client.gap_cache.get (hash);
    }
    while (block != nullptr);
}

rai::process_result rai::processor::process_receive (rai::block const & block_a)
{
    auto result (client.ledger.process (block_a));
    switch (result)
    {
        case rai::process_result::progress:
        {
            if (ledger_logging ())
            {
                std::string block;
                block_a.serialize_json (block);
                BOOST_LOG (client.log) << boost::str (boost::format ("Processing block %1% %2%") % block_a.hash().to_string () % block);
            }
            break;
        }
        case rai::process_result::gap_previous:
        {
            if (ledger_logging ())
            {
                BOOST_LOG (client.log) << boost::str (boost::format ("Gap previous for: %1%") % block_a.hash ().to_string ());
            }
            auto previous (block_a.previous ());
            client.gap_cache.add (block_a, previous);
            break;
        }
        case rai::process_result::gap_source:
        {
            if (ledger_logging ())
            {
                BOOST_LOG (client.log) << boost::str (boost::format ("Gap source for: %1%") % block_a.hash ().to_string ());
            }
            auto source (block_a.source ());
            client.gap_cache.add (block_a, source);
            break;
        }
        case rai::process_result::old:
        {
            if (ledger_duplicate_logging ())
            {
                BOOST_LOG (client.log) << boost::str (boost::format ("Old for: %1%") % block_a.hash ().to_string ());
            }
            break;
        }
        case rai::process_result::bad_signature:
        {
            if (ledger_logging ())
            {
                BOOST_LOG (client.log) << boost::str (boost::format ("Bad signature for: %1%") % block_a.hash ().to_string ());
            }
            break;
        }
        case rai::process_result::overspend:
        {
            if (ledger_logging ())
            {
                BOOST_LOG (client.log) << boost::str (boost::format ("Overspend for: %1%") % block_a.hash ().to_string ());
            }
            break;
        }
        case rai::process_result::overreceive:
        {
            if (ledger_logging ())
            {
                BOOST_LOG (client.log) << boost::str (boost::format ("Overreceive for: %1%") % block_a.hash ().to_string ());
            }
            break;
        }
        case rai::process_result::not_receive_from_send:
        {
            if (ledger_logging ())
            {
                BOOST_LOG (client.log) << boost::str (boost::format ("Not receive from spend for: %1%") % block_a.hash ().to_string ());
            }
            break;
        }
        case rai::process_result::fork_source:
        {
            if (ledger_logging ())
            {
                BOOST_LOG (client.log) << boost::str (boost::format ("Fork source for: %1%") % block_a.hash ().to_string ());
            }
            client.conflicts.start (*client.ledger.successor (block_a.root ()), false);
            break;
        }
        case rai::process_result::fork_previous:
        {
            if (ledger_logging ())
            {
                BOOST_LOG (client.log) << boost::str (boost::format ("Fork previous for: %1%") % block_a.hash ().to_string ());
            }
            client.conflicts.start (*client.ledger.successor (block_a.root ()), false);
            break;
        }
    }
    return result;
}

std::vector <rai::peer_information> rai::peer_container::list ()
{
    std::vector <rai::peer_information> result;
    std::lock_guard <std::mutex> lock (mutex);
    result.reserve (peers.size ());
    for (auto i (peers.begin ()), j (peers.end ()); i != j; ++i)
    {
        result.push_back (*i);
    }
    return result;
}

void rai::publish::visit (rai::message_visitor & visitor_a) const
{
    visitor_a.publish (*this);
}

rai::keepalive::keepalive () :
message (rai::message_type::keepalive)
{
    boost::asio::ip::udp::endpoint endpoint (boost::asio::ip::address_v6 {}, 0);
    for (auto i (peers.begin ()), n (peers.end ()); i != n; ++i)
    {
        *i = endpoint;
    }
}

void rai::keepalive::visit (rai::message_visitor & visitor_a) const
{
    visitor_a.keepalive (*this);
}

void rai::keepalive::serialize (rai::stream & stream_a)
{
    write_header (stream_a);
    for (auto i (peers.begin ()), j (peers.end ()); i != j; ++i)
    {
        assert (i->address ().is_v6 ());
        auto bytes (i->address ().to_v6 ().to_bytes ());
        write (stream_a, bytes);
        write (stream_a, i->port ());
    }
}

bool rai::keepalive::deserialize (rai::stream & stream_a)
{
	auto result (read_header (stream_a, version_max, version_using, version_min, type, extensions));
	assert (!result);
    assert (type == rai::message_type::keepalive);
    for (auto i (peers.begin ()), j (peers.end ()); i != j; ++i)
    {
        std::array <uint8_t, 16> address;
        uint16_t port;
        read (stream_a, address);
        read (stream_a, port);
        *i = rai::endpoint (boost::asio::ip::address_v6 (address), port);
    }
    return result;
}

size_t rai::processor_service::size ()
{
    std::lock_guard <std::mutex> lock (mutex);
    return operations.size ();
}

rai::system::system (uint16_t port_a, size_t count_a) :
service (new boost::asio::io_service)
{
    clients.reserve (count_a);
    for (size_t i (0); i < count_a; ++i)
    {
        rai::client_init init;
        auto client (std::make_shared <rai::client> (init, service, port_a + i, processor));
        assert (!init.error ());
        client->start ();
        clients.push_back (client);
    }
    for (auto i (clients.begin ()), j (clients.begin () + 1), n (clients.end ()); j != n; ++i, ++j)
    {
        auto starting1 ((*i)->peers.size ());
        auto new1 (starting1);
        auto starting2 ((*j)->peers.size ());
        auto new2 (starting2);
        (*j)->network.send_keepalive ((*i)->network.endpoint ());
        do {
            service->run_one ();
            new1 = (*i)->peers.size ();
            new2 = (*j)->peers.size ();
        } while (new1 == starting1 || new2 == starting2);
    }
}

rai::system::~system ()
{
    for (auto & i: clients)
    {
        i->stop ();
    }
}

std::shared_ptr <rai::wallet> rai::system::wallet (size_t index_a)
{
    assert (clients.size () > index_a);
    assert (clients [index_a]->wallets.items.size () == 1);
    return clients [index_a]->wallets.items.begin ()->second;
}

void rai::processor::process_confirmation (rai::block const & block_a, rai::endpoint const & sender)
{
    auto client_l (client.shared ());
    for (auto i (client.wallets.items.begin ()), n (client.wallets.items.end ()); i != n; ++i)
	{
		if (i->second->store.is_representative ())
        {
            auto representative (i->second->store.representative ());
			auto weight (client.ledger.weight (representative));
			if (!weight.is_zero ())
            {
                if (network_message_logging ())
                {
                    BOOST_LOG (client.log) << boost::str (boost::format ("Sending confirm ack to: %1%") % sender);
                }
                rai::private_key prv;
                auto error (i->second->store.fetch (representative, prv));
                assert (!error);
                client.network.confirm_block (prv, representative, block_a.clone (), 0, sender);
			}
		}
	}
}

rai::key_entry * rai::key_entry::operator -> ()
{
    return this;
}

rai::confirm_ack::confirm_ack () :
message (rai::message_type::confirm_ack)
{
}

rai::confirm_ack::confirm_ack (std::unique_ptr <rai::block> block_a) :
message (rai::message_type::confirm_ack)
{
    block_type_set (block_a->type ());
    vote.block = std::move (block_a);
}

bool rai::confirm_ack::deserialize (rai::stream & stream_a)
{
	auto result (read_header (stream_a, version_max, version_using, version_min, type, extensions));
	assert (!result);
    assert (type == rai::message_type::confirm_ack);
    if (!result)
    {
        result = read (stream_a, vote.account);
        if (!result)
        {
            result = read (stream_a, vote.signature);
            if (!result)
            {
                result = read (stream_a, vote.sequence);
                if (!result)
                {
                    vote.block = rai::deserialize_block (stream_a, block_type ());
                    result = vote.block == nullptr;
                }
            }
        }
    }
    return result;
}

void rai::confirm_ack::serialize (rai::stream & stream_a)
{
    assert (block_type () == rai::block_type::send || block_type () == rai::block_type::receive || block_type () == rai::block_type::open || block_type () == rai::block_type::change);
	write_header (stream_a);
    write (stream_a, vote.account);
    write (stream_a, vote.signature);
    write (stream_a, vote.sequence);
    vote.block->serialize (stream_a);
}

bool rai::confirm_ack::operator == (rai::confirm_ack const & other_a) const
{
    auto result (vote.account == other_a.vote.account && *vote.block == *other_a.vote.block && vote.signature == other_a.vote.signature && vote.sequence == other_a.vote.sequence);
    return result;
}

void rai::confirm_ack::visit (rai::message_visitor & visitor_a) const
{
    visitor_a.confirm_ack (*this);
}

rai::confirm_req::confirm_req () :
message (rai::message_type::confirm_req)
{
}

rai::confirm_req::confirm_req (std::unique_ptr <rai::block> block_a) :
message (rai::message_type::confirm_req),
block (std::move (block_a))
{
    block_type_set (block->type ());
}

bool rai::confirm_req::deserialize (rai::stream & stream_a)
{
	auto result (read_header (stream_a, version_max, version_using, version_min, type, extensions));
	assert (!result);
    assert (type == rai::message_type::confirm_req);
    if (!result)
	{
        block = rai::deserialize_block (stream_a, block_type ());
        result = block == nullptr;
    }
    return result;
}

void rai::confirm_req::visit (rai::message_visitor & visitor_a) const
{
    visitor_a.confirm_req (*this);
}

void rai::confirm_req::serialize (rai::stream & stream_a)
{
    assert (block != nullptr);
	write_header (stream_a);
    block->serialize (stream_a);
}

rai::rpc::rpc (boost::shared_ptr <boost::asio::io_service> service_a, boost::shared_ptr <boost::network::utils::thread_pool> pool_a, boost::asio::ip::address_v6 const & address_a, uint16_t port_a, rai::client & client_a, bool enable_control_a) :
server (decltype (server)::options (*this).address (address_a.to_string ()).port (std::to_string (port_a)).io_service (service_a).thread_pool (pool_a)),
client (client_a),
enable_control (enable_control_a)
{
}

void rai::rpc::start ()
{
    server.listen ();
}

void rai::rpc::stop ()
{
    server.stop ();
}

namespace
{
void set_response (boost::network::http::server <rai::rpc>::response & response, boost::property_tree::ptree & tree)
{
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, tree);
    response.status = boost::network::http::server <rai::rpc>::response::ok;
    response.headers.push_back (boost::network::http::response_header_narrow {"Content-Type", "application/json"});
    response.content = ostream.str ();
}
}

void rai::rpc::operator () (boost::network::http::server <rai::rpc>::request const & request, boost::network::http::server <rai::rpc>::response & response)
{
    if (request.method == "POST")
    {
        try
        {
            boost::property_tree::ptree request_l;
            std::stringstream istream (request.body);
            boost::property_tree::read_json (istream, request_l);
            std::string action (request_l.get <std::string> ("action"));
            if (log_rpc ())
            {
                BOOST_LOG (client.log) << request.body;
            }
            if (action == "account_balance_exact")
            {
                std::string account_text (request_l.get <std::string> ("account"));
                rai::uint256_union account;
                auto error (account.decode_base58check (account_text));
                if (!error)
                {
                    auto balance (client.ledger.account_balance (account));
                    boost::property_tree::ptree response_l;
                    response_l.put ("balance", balance.convert_to <std::string> ());
                    set_response (response, response_l);
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "Bad account number";
                }
            }
            else if (action == "account_balance")
            {
                std::string account_text (request_l.get <std::string> ("account"));
                rai::uint256_union account;
                auto error (account.decode_base58check (account_text));
                if (!error)
                {
                    auto balance (rai::scale_down (client.ledger.account_balance (account)));
                    boost::property_tree::ptree response_l;
                    response_l.put ("balance", std::to_string (balance));
                    set_response (response, response_l);
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "Bad account number";
                }
            }
            else if (action == "account_weight_exact")
            {
                std::string account_text (request_l.get <std::string> ("account"));
                rai::uint256_union account;
                auto error (account.decode_base58check (account_text));
                if (!error)
                {
                    auto balance (client.ledger.weight (account));
                    boost::property_tree::ptree response_l;
                    response_l.put ("weight", balance.convert_to <std::string> ());
                    set_response (response, response_l);
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "Bad account number";
                }
            }
            else if (action == "account_weight")
            {
                std::string account_text (request_l.get <std::string> ("account"));
                rai::uint256_union account;
                auto error (account.decode_base58check (account_text));
                if (!error)
                {
                    auto balance (rai::scale_down (client.ledger.weight (account)));
                    boost::property_tree::ptree response_l;
                    response_l.put ("weight", std::to_string (balance));
                    set_response (response, response_l);
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "Bad account number";
                }
            }
            else if (action == "account_create")
            {
                if (enable_control)
                {
                    std::string wallet_text (request_l.get <std::string> ("wallet"));
                    rai::uint256_union wallet;
                    auto error (wallet.decode_hex (wallet_text));
                    if (!error)
                    {
                        auto existing (client.wallets.items.find (wallet));
                        if (existing != client.wallets.items.end ())
                        {
                            rai::keypair new_key;
                            existing->second->store.insert (new_key.prv);
                            boost::property_tree::ptree response_l;
                            std::string account;
                            new_key.pub.encode_base58check (account);
                            response_l.put ("account", account);
                            set_response (response, response_l);
                        }
                        else
                        {
                            response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                            response.content = "Wallet not found";
                        }
                    }
                    else
                    {
                        response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                        response.content = "Bad wallet number";
                    }
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "RPC control is disabled";
                }
            }
            else if (action == "wallet_contains")
            {
                std::string account_text (request_l.get <std::string> ("account"));
                std::string wallet_text (request_l.get <std::string> ("wallet"));
                rai::uint256_union account;
                auto error (account.decode_base58check (account_text));
                if (!error)
                {
                    rai::uint256_union wallet;
                    auto error (wallet.decode_hex (wallet_text));
                    if (!error)
                    {
                        auto existing (client.wallets.items.find (wallet));
                        if (existing != client.wallets.items.end ())
                        {
                            auto exists (existing->second->store.find (account) != existing->second->store.end ());
                            boost::property_tree::ptree response_l;
                            response_l.put ("exists", exists ? "1" : "0");
                            set_response (response, response_l);
                        }
                        else
                        {
                            response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                            response.content = "Wallet not found";
                        }
                    }
                    else
                    {
                        response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                        response.content = "Bad wallet number";
                    }
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "Bad account number";
                }
            }
            else if (action == "account_list")
            {
                std::string wallet_text (request_l.get <std::string> ("wallet"));
                rai::uint256_union wallet;
                auto error (wallet.decode_hex (wallet_text));
                if (!error)
                {
                    auto existing (client.wallets.items.find (wallet));
                    if (existing != client.wallets.items.end ())
                    {
                        boost::property_tree::ptree response_l;
                        boost::property_tree::ptree accounts;
                        for (auto i (existing->second->store.begin ()), j (existing->second->store.end ()); i != j; ++i)
                        {
                            std::string account;
                            i->first.encode_base58check (account);
                            boost::property_tree::ptree entry;
                            entry.put ("", account);
                            accounts.push_back (std::make_pair ("", entry));
                        }
                        response_l.add_child ("accounts", accounts);
                        set_response (response, response_l);
                    }
                    else
                    {
                        response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                        response.content = "Wallet not found";
                    }
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "Bad wallet number";
                }
            }
            else if (action == "wallet_add")
            {
                if (enable_control)
                {
                    std::string key_text (request_l.get <std::string> ("key"));
                    std::string wallet_text (request_l.get <std::string> ("wallet"));
                    rai::private_key key;
                    auto error (key.decode_hex (key_text));
                    if (!error)
                    {
                        rai::uint256_union wallet;
                        auto error (wallet.decode_hex (wallet_text));
                        if (!error)
                        {
                            auto existing (client.wallets.items.find (wallet));
                            if (existing != client.wallets.items.end ())
                            {
                                existing->second->store.insert (key);
                                rai::public_key pub;
                                ed25519_publickey (key.bytes.data (), pub.bytes.data ());
                                std::string account;
                                pub.encode_base58check (account);
                                boost::property_tree::ptree response_l;
                                response_l.put ("account", account);
                                set_response (response, response_l);
                            }
                            else
                            {
                                response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                                response.content = "Wallet not found";
                            }
                        }
                        else
                        {
                            response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                            response.content = "Bad wallet number";
                        }
                    }
                    else
                    {
                        response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                        response.content = "Bad private key";
                    }
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "RPC control is disabled";
                }
            }
            else if (action == "wallet_key_valid")
            {
                if (enable_control)
                {
                    std::string wallet_text (request_l.get <std::string> ("wallet"));
                    rai::uint256_union wallet;
                    auto error (wallet.decode_hex (wallet_text));
                    if (!error)
                    {
                        auto existing (client.wallets.items.find (wallet));
                        if (existing != client.wallets.items.end ())
                        {
                            auto valid (existing->second->store.valid_password ());
                            boost::property_tree::ptree response_l;
                            response_l.put ("valid", valid ? "1" : "0");
                            set_response (response, response_l);
                        }
                        else
                        {
                            response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                            response.content = "Wallet not found";
                        }
                    }
                    else
                    {
                        response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                        response.content = "Bad wallet number";
                    }
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "RPC control is disabled";
                }
            }
            else if (action == "validate_account_number")
            {
                std::string account_text (request_l.get <std::string> ("account"));
                rai::uint256_union account;
                auto error (account.decode_base58check (account_text));
                boost::property_tree::ptree response_l;
                response_l.put ("valid", error ? "0" : "1");
                set_response (response, response_l);
            }
            else if (action == "send_exact")
            {
                if (enable_control)
                {
                    std::string wallet_text (request_l.get <std::string> ("wallet"));
                    rai::uint256_union wallet;
                    auto error (wallet.decode_hex (wallet_text));
                    if (!error)
                    {
                        auto existing (client.wallets.items.find (wallet));
                        if (existing != client.wallets.items.end ())
                        {
                            std::string account_text (request_l.get <std::string> ("account"));
                            rai::uint256_union account;
                            auto error (account.decode_base58check (account_text));
                            if (!error)
                            {
                                std::string amount_text (request_l.get <std::string> ("amount"));
                                rai::amount amount;
                                auto error (amount.decode_dec (amount_text));
                                if (!error)
                                {
                                    auto error (existing->second->send (account, amount.number ()));
                                    boost::property_tree::ptree response_l;
                                    response_l.put ("sent", error ? "0" : "1");
                                    set_response (response, response_l);
                                }
                                else
                                {
                                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                                    response.content = "Bad amount format";
                                }
                            }
                            else
                            {
                                response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                                response.content = "Bad account number";
                            }
                        }
                        else
                        {
                            response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                            response.content = "Wallet not found";
                        }
                    }
                    else
                    {
                        response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                        response.content = "Bad wallet number";
                    }
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "RPC control is disabled";
                }
            }
            else if (action == "send")
            {
                if (enable_control)
                {
                    std::string wallet_text (request_l.get <std::string> ("wallet"));
                    rai::uint256_union wallet;
                    auto error (wallet.decode_hex (wallet_text));
                    if (!error)
                    {
                        auto existing (client.wallets.items.find (wallet));
                        if (existing != client.wallets.items.end ())
                        {
                            std::string account_text (request_l.get <std::string> ("account"));
                            rai::uint256_union account;
                            auto error (account.decode_base58check (account_text));
                            if (!error)
                            {
                                std::string amount_text (request_l.get <std::string> ("amount"));
                                try
                                {
                                    uint64_t amount_number (std::stoull (amount_text));
                                    auto amount (rai::scale_up (amount_number));
                                    auto error (existing->second->send (account, amount));
                                    boost::property_tree::ptree response_l;
                                    response_l.put ("sent", error ? "0" : "1");
                                    set_response (response, response_l);
                                }
                                catch (std::logic_error const &)
                                {
                                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                                    response.content = "Bad amount format";
                                }
                            }
                            else
                            {
                                response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                                response.content = "Bad account number";
                            }
                        }
                        else
                        {
                            response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                            response.content = "Wallet not found";
                        }
                    }
                    else
                    {
                        response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                        response.content = "Bad wallet number";
                    }
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "RPC control is disabled";
                }
            }
            else if (action == "password_valid")
            {
                std::string wallet_text (request_l.get <std::string> ("wallet"));
                rai::uint256_union wallet;
                auto error (wallet.decode_hex (wallet_text));
                if (!error)
                {
                    auto existing (client.wallets.items.find (wallet));
                    if (existing != client.wallets.items.end ())
                    {
                        boost::property_tree::ptree response_l;
                        response_l.put ("valid", existing->second->store.valid_password () ? "1" : "0");
                        set_response (response, response_l);
                    }
                    else
                    {
                        response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                        response.content = "Wallet not found";
                    }
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "Bad account number";
                }
            }
            else if (action == "password_change")
            {
                std::string wallet_text (request_l.get <std::string> ("wallet"));
                rai::uint256_union wallet;
                auto error (wallet.decode_hex (wallet_text));
                if (!error)
                {
                    auto existing (client.wallets.items.find (wallet));
                    if (existing != client.wallets.items.end ())
                    {
                        boost::property_tree::ptree response_l;
                        std::string password_text (request_l.get <std::string> ("password"));
                        auto error (existing->second->store.rekey (password_text));
                        response_l.put ("changed", error ? "0" : "1");
                        set_response (response, response_l);
                    }
                    else
                    {
                        response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                        response.content = "Wallet not found";
                    }
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "Bad account number";
                }
            }
            else if (action == "password_enter")
            {
                std::string wallet_text (request_l.get <std::string> ("wallet"));
                rai::uint256_union wallet;
                auto error (wallet.decode_hex (wallet_text));
                if (!error)
                {
                    auto existing (client.wallets.items.find (wallet));
                    if (existing != client.wallets.items.end ())
                    {
                        boost::property_tree::ptree response_l;
                        std::string password_text (request_l.get <std::string> ("password"));
                        existing->second->store.enter_password (password_text);
                        response_l.put ("valid", existing->second->store.valid_password () ? "1" : "0");
                        set_response (response, response_l);
                    }
                    else
                    {
                        response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                        response.content = "Wallet not found";
                    }
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "Bad account number";
                }
            }
            else if (action == "representative")
            {
                std::string wallet_text (request_l.get <std::string> ("wallet"));
                rai::uint256_union wallet;
                auto error (wallet.decode_hex (wallet_text));
                if (!error)
                {
                    auto existing (client.wallets.items.find (wallet));
                    if (existing != client.wallets.items.end ())
                    {
                        boost::property_tree::ptree response_l;
                        std::string representative;
                        existing->second->store.representative ().encode_base58check (representative);
                        response_l.put ("representative", representative);
                        set_response (response, response_l);
                    }
                    else
                    {
                        response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                        response.content = "Wallet not found";
                    }
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "Bad account number";
                }
            }
            else if (action == "representative_set")
            {
                std::string wallet_text (request_l.get <std::string> ("wallet"));
                rai::uint256_union wallet;
                auto error (wallet.decode_hex (wallet_text));
                if (!error)
                {
                    auto existing (client.wallets.items.find (wallet));
                    if (existing != client.wallets.items.end ())
                    {
                        std::string representative_text (request_l.get <std::string> ("representative"));
                        rai::account representative;
                        representative.decode_base58check (representative_text);
                        existing->second->store.representative_set (representative);
                        boost::property_tree::ptree response_l;
                        set_response (response, response_l);
                    }
                    else
                    {
                        response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                        response.content = "Wallet not found";
                    }
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "Bad account number";
                }
            }
            else if (action == "wallet_create")
            {
                rai::keypair wallet_id;
                auto wallet (client.wallets.create (wallet_id.prv));
                boost::property_tree::ptree response_l;
                response_l.put ("wallet", wallet_id.prv.to_string ());
                set_response (response, response_l);
            }
            else if (action == "wallet_export")
            {
                std::string wallet_text (request_l.get <std::string> ("wallet"));
                rai::uint256_union wallet;
                auto error (wallet.decode_hex (wallet_text));
                if (!error)
                {
                    auto existing (client.wallets.items.find (wallet));
                    if (existing != client.wallets.items.end ())
                    {
                        std::string json;
                        existing->second->store.serialize_json (json);
                        boost::property_tree::ptree response_l;
                        response_l.put ("json", json);
                        set_response (response, response_l);
                    }
                    else
                    {
                        response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                        response.content = "Wallet not found";
                    }
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "Bad account number";
                }
            }
            else if (action == "wallet_destroy")
            {
                std::string wallet_text (request_l.get <std::string> ("wallet"));
                rai::uint256_union wallet;
                auto error (wallet.decode_hex (wallet_text));
                if (!error)
                {
                    auto existing (client.wallets.items.find (wallet));
                    if (existing != client.wallets.items.end ())
                    {
                        client.wallets.destroy (wallet);
                        boost::property_tree::ptree response_l;
                        set_response (response, response_l);
                    }
                    else
                    {
                        response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                        response.content = "Wallet not found";
                    }
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "Bad wallet number";
                }
            }
            else if (action == "account_move")
            {
                std::string wallet_text (request_l.get <std::string> ("wallet"));
                std::string source_text (request_l.get <std::string> ("source"));
                auto accounts_text (request_l.get_child ("accounts"));
                rai::uint256_union wallet;
                auto error (wallet.decode_hex (wallet_text));
                if (!error)
                {
                    auto existing (client.wallets.items.find (wallet));
                    if (existing != client.wallets.items.end ())
                    {
                        auto wallet (existing->second);
                        rai::uint256_union source;
                        auto error (source.decode_hex (source_text));
                        if (!error)
                        {
                            auto existing (client.wallets.items.find (source));
                            if (existing != client.wallets.items.end ())
                            {
                                auto source (existing->second);
                                std::vector <rai::public_key> accounts;
                                for (auto i (accounts_text.begin ()), n (accounts_text.end ()); i != n; ++i)
                                {
                                    rai::public_key account;
                                    account.decode_hex (i->second.get <std::string> (""));
                                    accounts.push_back (account);
                                }
                                auto error (wallet->store.move (source->store, accounts));
                                boost::property_tree::ptree response_l;
                                response_l.put ("moved", error ? "0" : "1");
                                set_response (response, response_l);
                            }
                            else
                            {
                                response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                                response.content = "Source not found";
                            }
                        }
                        else
                        {
                            response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                            response.content = "Bad source number";
                        }
                    }
                    else
                    {
                        response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                        response.content = "Wallet not found";
                    }
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "Bad wallet number";
                }
            }
            else
            {
                response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                response.content = "Unknown command";
            }
        }
        catch (std::runtime_error const &)
        {
            response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
            response.content = "Unable to parse JSON";
        }
    }
    else
    {
        response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::method_not_allowed);
        response.content = "Can only POST requests";
    }
}

namespace
{
class rollback_visitor : public rai::block_visitor
{
public:
    rollback_visitor (rai::ledger & ledger_a) :
    ledger (ledger_a)
    {
    }
    void send_block (rai::send_block const & block_a) override
    {
		auto hash (block_a.hash ());
        rai::receivable receivable;
		while (ledger.store.pending_get (hash, receivable))
		{
			ledger.rollback (ledger.latest (block_a.hashables.destination));
		}
        rai::frontier frontier;
        ledger.store.latest_get (receivable.source, frontier);
		ledger.store.pending_del (hash);
        ledger.change_latest (receivable.source, block_a.hashables.previous, frontier.representative, ledger.balance (block_a.hashables.previous));
		ledger.store.block_del (hash);
    }
    void receive_block (rai::receive_block const & block_a) override
    {
		auto hash (block_a.hash ());
        auto representative (ledger.representative (block_a.hashables.source));
        auto amount (ledger.amount (block_a.hashables.source));
        auto destination_account (ledger.account (hash));
		ledger.move_representation (ledger.representative (hash), representative, amount);
        ledger.change_latest (destination_account, block_a.hashables.previous, representative, ledger.balance (block_a.hashables.previous));
		ledger.store.block_del (hash);
        ledger.store.pending_put (block_a.hashables.source, {ledger.account (block_a.hashables.source), amount, destination_account});
    }
    void open_block (rai::open_block const & block_a) override
    {
		auto hash (block_a.hash ());
        auto representative (ledger.representative (block_a.hashables.source));
        auto amount (ledger.amount (block_a.hashables.source));
        auto destination_account (ledger.account (hash));
		ledger.move_representation (ledger.representative (hash), representative, amount);
        ledger.change_latest (destination_account, 0, representative, 0);
		ledger.store.block_del (hash);
        ledger.store.pending_put (block_a.hashables.source, {ledger.account (block_a.hashables.source), amount, destination_account});
    }
    void change_block (rai::change_block const & block_a) override
    {
        auto representative (ledger.representative (block_a.hashables.previous));
        auto account (ledger.account (block_a.hashables.previous));
        rai::frontier frontier;
        ledger.store.latest_get (account, frontier);
		ledger.move_representation (block_a.hashables.representative, representative, ledger.balance (block_a.hashables.previous));
		ledger.store.block_del (block_a.hash ());
        ledger.change_latest (account, block_a.hashables.previous, representative, frontier.balance);
    }
    rai::ledger & ledger;
};
}

namespace
{
bool parse_address_port (std::string const & string, boost::asio::ip::address & address_a, uint16_t & port_a)
{
    auto result (false);
    auto port_position (string.rfind (':'));
    if (port_position != std::string::npos && port_position > 0)
    {
        std::string port_string (string.substr (port_position + 1));
        try
        {
            size_t converted;
            auto port (std::stoul (port_string, &converted));
            if (port <= std::numeric_limits <uint16_t>::max () && converted == port_string.size ())
            {
                boost::system::error_code ec;
                auto address (boost::asio::ip::address_v4::from_string (string.substr (0, port_position), ec));
                if (ec == 0)
                {
                    address_a = address;
                    port_a = port;
                }
                else
                {
                    result = true;
                }
            }
            else
            {
                result = true;
            }
        }
        catch (...)
        {
            result = true;
        }
    }
    else
    {
        result = true;
    }
    return result;
}
}

bool rai::parse_endpoint (std::string const & string, rai::endpoint & endpoint_a)
{
    boost::asio::ip::address address;
    uint16_t port;
    auto result (parse_address_port (string, address, port));
    if (!result)
    {
        endpoint_a = rai::endpoint (address, port);
    }
    return result;
}

bool rai::parse_tcp_endpoint (std::string const & string, rai::tcp_endpoint & endpoint_a)
{
    boost::asio::ip::address address;
    uint16_t port;
    auto result (parse_address_port (string, address, port));
    if (!result)
    {
        endpoint_a = rai::tcp_endpoint (address, port);
    }
    return result;
}

rai::bulk_pull::bulk_pull () :
message (rai::message_type::bulk_pull)
{
}

void rai::bulk_pull::visit (rai::message_visitor & visitor_a) const
{
    visitor_a.bulk_pull (*this);
}

bool rai::bulk_pull::deserialize (rai::stream & stream_a)
{
	auto result (read_header (stream_a, version_max, version_using, version_min, type, extensions));
	assert (!result);
	assert (rai::message_type::bulk_pull == type);
    if (!result)
    {
        assert (type == rai::message_type::bulk_pull);
        result = read (stream_a, start);
        if (!result)
        {
            result = read (stream_a, end);
        }
    }
    return result;
}

void rai::bulk_pull::serialize (rai::stream & stream_a)
{
	write_header (stream_a);
    write (stream_a, start);
    write (stream_a, end);
}

rai::bulk_push::bulk_push () :
message (rai::message_type::bulk_push)
{
}

bool rai::bulk_push::deserialize (rai::stream & stream_a)
{
    auto result (read_header (stream_a, version_max, version_using, version_min, type, extensions));
    assert (!result);
    assert (rai::message_type::bulk_push == type);
    return result;
}

void rai::bulk_push::serialize (rai::stream & stream_a)
{
    write_header (stream_a);
}

void rai::bulk_push::visit (rai::message_visitor & visitor_a) const
{
    visitor_a.bulk_push (*this);
}

void rai::client::start ()
{
    network.receive ();
    processor.ongoing_keepalive ();
    bootstrap.start ();
}

void rai::client::stop ()
{
    BOOST_LOG (log) << "Client stopping";
    network.stop ();
    bootstrap.stop ();
    service.stop ();
}

void rai::processor::bootstrap (boost::asio::ip::tcp::endpoint const & endpoint_a, std::function <void ()> const & completion_action_a)
{
    auto processor (std::make_shared <rai::bootstrap_client> (client.shared (), completion_action_a));
    processor->run (endpoint_a);
}

void rai::processor::connect_bootstrap (std::vector <std::string> const & peers_a)
{
    auto client_l (client.shared ());
    client.service.add (std::chrono::system_clock::now (), [client_l, peers_a] ()
    {
        for (auto i (peers_a.begin ()), n (peers_a.end ()); i != n; ++i)
        {
            client_l->network.resolver.async_resolve (boost::asio::ip::udp::resolver::query (*i, std::to_string (rai::network::node_port)), [client_l] (boost::system::error_code const & ec, boost::asio::ip::udp::resolver::iterator i_a)
            {
                if (!ec)
                {
                    for (auto i (i_a), n (boost::asio::ip::udp::resolver::iterator {}); i != n; ++i)
                    {
                        client_l->send_keepalive (i->endpoint ());
                    }
                }
            });
        }
    });
}

void rai::processor::search_pending ()
{
    auto client_l (client.shared ());
    client.service.add (std::chrono::system_clock::now (), [client_l] ()
    {
        std::unordered_set <rai::uint256_union> wallet;
        for (auto i (client_l->wallets.items.begin ()), n (client_l->wallets.items.end ()); i != n; ++i)
        {
            for (auto j (i->second->store.begin ()), m (i->second->store.end ()); j != m; ++j)
            {
                wallet.insert (j->first);
            }
        }
        for (auto i (client_l->store.pending_begin ()), n (client_l->store.pending_end ()); i != n; ++i)
        {
            if (wallet.find (i->second.destination) != wallet.end ())
            {
                auto block (client_l->store.block_get (i->first));
                assert (block != nullptr);
                assert (dynamic_cast <rai::send_block *> (block.get ()) != nullptr);
                client_l->conflicts.start (*block, true);
            }
        }
    });
}

rai::bootstrap_initiator::bootstrap_initiator (rai::client & client_a) :
client (client_a),
in_progress (false),
warmed_up (false)
{
}

void rai::bootstrap_initiator::warmup (rai::endpoint const & endpoint_a)
{
	std::lock_guard <std::mutex> lock (mutex);
	if (!warmed_up && !in_progress)
	{
		warmed_up = true;
		in_progress = true;
		initiate (endpoint_a);
	}
}

void rai::bootstrap_initiator::bootstrap (rai::endpoint const & endpoint_a)
{
	std::lock_guard <std::mutex> lock (mutex);
	if (!in_progress)
	{
		initiate (endpoint_a);
	}
}

void rai::bootstrap_initiator::bootstrap_any ()
{
    auto list (client.peers.list ());
    if (!list.empty ())
    {
        bootstrap (list [0].endpoint);
    }
}

void rai::bootstrap_initiator::initiate (rai::endpoint const & endpoint_a)
{
	client.processor.bootstrap (rai::tcp_endpoint (endpoint_a.address (), endpoint_a.port ()), [this] ()
	{
		std::lock_guard <std::mutex> lock (mutex);
		in_progress = false;
	});
}

rai::bootstrap_listener::bootstrap_listener (boost::asio::io_service & service_a, uint16_t port_a, rai::client & client_a) :
acceptor (service_a),
local (boost::asio::ip::tcp::endpoint (boost::asio::ip::address_v6::any (), port_a)),
service (service_a),
client (client_a)
{
}

void rai::bootstrap_listener::start ()
{
    acceptor.open (local.protocol ());
    acceptor.set_option (boost::asio::ip::tcp::acceptor::reuse_address (true));
    acceptor.bind (local);
    acceptor.listen ();
    accept_connection ();
}

void rai::bootstrap_listener::stop ()
{
    on = false;
    acceptor.close ();
}

void rai::bootstrap_listener::accept_connection ()
{
    auto socket (std::make_shared <boost::asio::ip::tcp::socket> (service));
    acceptor.async_accept (*socket, [this, socket] (boost::system::error_code const & ec)
    {
        accept_action (ec, socket);
    });
}

void rai::bootstrap_listener::accept_action (boost::system::error_code const & ec, std::shared_ptr <boost::asio::ip::tcp::socket> socket_a)
{
    if (!ec)
    {
        accept_connection ();
        auto connection (std::make_shared <rai::bootstrap_server> (socket_a, client.shared ()));
        connection->receive ();
    }
    else
    {
        BOOST_LOG (client.log) << boost::str (boost::format ("Error while accepting bootstrap connections: %1%") % ec.message ());
    }
}

rai::bootstrap_server::bootstrap_server (std::shared_ptr <boost::asio::ip::tcp::socket> socket_a, std::shared_ptr <rai::client> client_a) :
socket (socket_a),
client (client_a)
{
}

void rai::bootstrap_server::receive ()
{
    auto this_l (shared_from_this ());
    boost::asio::async_read (*socket, boost::asio::buffer (receive_buffer.data (), 8), [this_l] (boost::system::error_code const & ec, size_t size_a)
    {
        this_l->receive_header_action (ec, size_a);
    });
}

void rai::bootstrap_server::receive_header_action (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
        assert (size_a == 8);
		rai::bufferstream type_stream (receive_buffer.data (), size_a);
		uint8_t version_max;
		uint8_t version_using;
		uint8_t version_min;
		rai::message_type type;
		std::bitset <16> extensions;
		if (!rai::message::read_header (type_stream, version_max, version_using, version_min, type, extensions))
		{
			switch (type)
			{
				case rai::message_type::bulk_pull:
				{
					auto this_l (shared_from_this ());
					boost::asio::async_read (*socket, boost::asio::buffer (receive_buffer.data () + 8, sizeof (rai::uint256_union) + sizeof (rai::uint256_union)), [this_l] (boost::system::error_code const & ec, size_t size_a)
					{
						this_l->receive_bulk_pull_action (ec, size_a);
					});
					break;
				}
				case rai::message_type::frontier_req:
				{
					auto this_l (shared_from_this ());
					boost::asio::async_read (*socket, boost::asio::buffer (receive_buffer.data () + 8, sizeof (rai::uint256_union) + sizeof (uint32_t) + sizeof (uint32_t)), [this_l] (boost::system::error_code const & ec, size_t size_a)
					{
						this_l->receive_frontier_req_action (ec, size_a);
					});
					break;
				}
                case rai::message_type::bulk_push:
                {
                    add_request (std::unique_ptr <rai::message> (new rai::bulk_push));
                    break;
                }
				default:
				{
					if (network_logging ())
					{
						BOOST_LOG (client->log) << boost::str (boost::format ("Received invalid type from bootstrap connection %1%") % static_cast <uint8_t> (type));
					}
					break;
				}
			}
		}
    }
    else
    {
        if (network_logging ())
        {
            BOOST_LOG (client->log) << boost::str (boost::format ("Error while receiving type %1%") % ec.message ());
        }
    }
}

void rai::bootstrap_server::receive_bulk_pull_action (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
        std::unique_ptr <rai::bulk_pull> request (new rai::bulk_pull);
        rai::bufferstream stream (receive_buffer.data (), 8 + sizeof (rai::uint256_union) + sizeof (rai::uint256_union));
        auto error (request->deserialize (stream));
        if (!error)
        {
            if (network_logging ())
            {
                BOOST_LOG (client->log) << boost::str (boost::format ("Received bulk pull for %1% down to %2%") % request->start.to_string () % request->end.to_string ());
            }
			add_request (std::unique_ptr <rai::message> (request.release ()));
            receive ();
        }
    }
}

void rai::bootstrap_server::receive_frontier_req_action (boost::system::error_code const & ec, size_t size_a)
{
	if (!ec)
	{
		std::unique_ptr <rai::frontier_req> request (new rai::frontier_req);
		rai::bufferstream stream (receive_buffer.data (), 8 + sizeof (rai::uint256_union) + sizeof (uint32_t) + sizeof (uint32_t));
		auto error (request->deserialize (stream));
		if (!error)
		{
			if (network_logging ())
			{
				BOOST_LOG (client->log) << boost::str (boost::format ("Received frontier request for %1% with age %2%") % request->start.to_string () % request->age);
			}
			add_request (std::unique_ptr <rai::message> (request.release ()));
			receive ();
		}
	}
    else
    {
        if (network_logging ())
        {
            BOOST_LOG (client->log) << boost::str (boost::format ("Error sending receiving frontier request %1%") % ec.message ());
        }
    }
}

void rai::bootstrap_server::add_request (std::unique_ptr <rai::message> message_a)
{
	std::lock_guard <std::mutex> lock (mutex);
    auto start (requests.empty ());
	requests.push (std::move (message_a));
	if (start)
	{
		run_next ();
	}
}

void rai::bootstrap_server::finish_request ()
{
	std::lock_guard <std::mutex> lock (mutex);
	requests.pop ();
	if (!requests.empty ())
	{
		run_next ();
	}
}

namespace
{
class request_response_visitor : public rai::message_visitor
{
public:
    request_response_visitor (std::shared_ptr <rai::bootstrap_server> connection_a) :
    connection (connection_a)
    {
    }
    void keepalive (rai::keepalive const &) override
    {
        assert (false);
    }
    void publish (rai::publish const &) override
    {
        assert (false);
    }
    void confirm_req (rai::confirm_req const &) override
    {
        assert (false);
    }
    void confirm_ack (rai::confirm_ack const &) override
    {
        assert (false);
    }
    void bulk_pull (rai::bulk_pull const &) override
    {
        auto response (std::make_shared <rai::bulk_pull_server> (connection, std::unique_ptr <rai::bulk_pull> (static_cast <rai::bulk_pull *> (connection->requests.front ().release ()))));
        response->send_next ();
    }
    void bulk_push (rai::bulk_push const &) override
    {
        auto response (std::make_shared <rai::bulk_push_server> (connection));
        response->receive ();
    }
    void frontier_req (rai::frontier_req const &) override
    {
        auto response (std::make_shared <rai::frontier_req_server> (connection, std::unique_ptr <rai::frontier_req> (static_cast <rai::frontier_req *> (connection->requests.front ().release ()))));
        response->send_next ();
    }
    std::shared_ptr <rai::bootstrap_server> connection;
};
}

void rai::bootstrap_server::run_next ()
{
	assert (!requests.empty ());
    request_response_visitor visitor (shared_from_this ());
    requests.front ()->visit (visitor);
}

void rai::bulk_pull_server::set_current_end ()
{
    assert (request != nullptr);
    auto end_exists (request->end.is_zero () || connection->client->store.block_exists (request->end));
    if (end_exists)
    {
        rai::frontier frontier;
        auto no_address (connection->client->store.latest_get (request->start, frontier));
        if (no_address)
        {
            current = request->end;
        }
        else
        {
            if (!request->end.is_zero ())
            {
                auto account (connection->client->ledger.account (request->end));
                if (account == request->start)
                {
                    current = frontier.hash;
                }
                else
                {
                    current = request->end;
                }
            }
            else
            {
                current = frontier.hash;
            }
        }
    }
    else
    {
        current = request->end;
    }
}

void rai::bulk_pull_server::send_next ()
{
    std::unique_ptr <rai::block> block (get_next ());
    if (block != nullptr)
    {
        {
            send_buffer.clear ();
            rai::vectorstream stream (send_buffer);
            rai::serialize_block (stream, *block);
        }
        auto this_l (shared_from_this ());
        if (network_logging ())
        {
            BOOST_LOG (connection->client->log) << boost::str (boost::format ("Sending block: %1%") % block->hash ().to_string ());
        }
        async_write (*connection->socket, boost::asio::buffer (send_buffer.data (), send_buffer.size ()), [this_l] (boost::system::error_code const & ec, size_t size_a)
        {
            this_l->sent_action (ec, size_a);
        });
    }
    else
    {
        send_finished ();
    }
}

std::unique_ptr <rai::block> rai::bulk_pull_server::get_next ()
{
    std::unique_ptr <rai::block> result;
    if (current != request->end)
    {
        result = connection->client->store.block_get (current);
        assert (result != nullptr);
        auto previous (result->previous ());
        if (!previous.is_zero ())
        {
            current = previous;
        }
        else
        {
            request->end = current;
        }
    }
    return result;
}

void rai::bulk_pull_server::sent_action (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
        send_next ();
    }
}

void rai::bulk_pull_server::send_finished ()
{
    send_buffer.clear ();
    send_buffer.push_back (static_cast <uint8_t> (rai::block_type::not_a_block));
    auto this_l (shared_from_this ());
    if (network_logging ())
    {
        BOOST_LOG (connection->client->log) << "Bulk sending finished";
    }
    async_write (*connection->socket, boost::asio::buffer (send_buffer.data (), 1), [this_l] (boost::system::error_code const & ec, size_t size_a)
    {
        this_l->no_block_sent (ec, size_a);
    });
}

void rai::bulk_pull_server::no_block_sent (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
        assert (size_a == 1);
		connection->finish_request ();
    }
}

rai::account_iterator rai::block_store::latest_begin (rai::account const & account_a)
{
    rai::account_iterator result (*accounts, account_a);
    return result;
}

rai::bootstrap_client::bootstrap_client (std::shared_ptr <rai::client> client_a, std::function <void ()> const & completion_action_a) :
client (client_a),
socket (client_a->network.service),
completion_action (completion_action_a)
{
}

rai::bootstrap_client::~bootstrap_client ()
{
	if (network_logging ())
	{
		BOOST_LOG (client->log) << "Exiting bootstrap processor";
	}
	completion_action ();
}

void rai::bootstrap_client::run (boost::asio::ip::tcp::endpoint const & endpoint_a)
{
    if (network_logging ())
    {
        BOOST_LOG (client->log) << boost::str (boost::format ("Initiating bootstrap connection to %1%") % endpoint_a);
    }
    auto this_l (shared_from_this ());
    socket.async_connect (endpoint_a, [this_l] (boost::system::error_code const & ec)
    {
        this_l->connect_action (ec);
    });
}

void rai::bootstrap_client::connect_action (boost::system::error_code const & ec)
{
    if (!ec)
    {
        std::unique_ptr <rai::frontier_req> request (new rai::frontier_req);
        request->start.clear ();
        request->age = std::numeric_limits <decltype (request->age)>::max ();
        request->count = std::numeric_limits <decltype (request->age)>::max ();
        auto send_buffer (std::make_shared <std::vector <uint8_t>> ());
        {
            rai::vectorstream stream (*send_buffer);
            request->serialize (stream);
        }
        auto this_l (shared_from_this ());
        boost::asio::async_write (socket, boost::asio::buffer (send_buffer->data (), send_buffer->size ()), [this_l, send_buffer] (boost::system::error_code const & ec, size_t size_a)
        {
            this_l->sent_request (ec, size_a);
        });
    }
    else
    {
        if (network_logging ())
        {
            BOOST_LOG (client->log) << boost::str (boost::format ("Error initiating bootstrap connection %1%") % ec.message ());
        }
    }
}

void rai::bootstrap_client::sent_request (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
        auto this_l (shared_from_this ());
        auto client_l (std::make_shared <rai::frontier_req_client> (this_l));
        client_l->receive_frontier ();
    }
    else
    {
        if (network_logging ())
        {
            BOOST_LOG (client->log) << boost::str (boost::format ("Error while sending bootstrap request %1%") % ec.message ());
        }
    }
}

void rai::bulk_pull_client::request ()
{
    if (current != end)
    {
        rai::bulk_pull req;
        req.start = current->first;
        req.end = current->second;
        ++current;
        auto buffer (std::make_shared <std::vector <uint8_t>> ());
        {
            rai::vectorstream stream (*buffer);
            req.serialize (stream);
        }
        auto this_l (shared_from_this ());
        boost::asio::async_write (connection->connection->socket, boost::asio::buffer (buffer->data (), buffer->size ()), [this_l, buffer] (boost::system::error_code const & ec, size_t size_a)
            {
                if (!ec)
                {
                    this_l->receive_block ();
                }
                else
                {
                    BOOST_LOG (this_l->connection->connection->client->log) << boost::str (boost::format ("Error sending bulk pull request %1%") % ec.message ());
                }
            });
    }
    else
    {
        process_end ();
        connection->completed_pulls ();
    }
}

void rai::bulk_pull_client::receive_block ()
{
    auto this_l (shared_from_this ());
    boost::asio::async_read (connection->connection->socket, boost::asio::buffer (receive_buffer.data (), 1), [this_l] (boost::system::error_code const & ec, size_t size_a)
    {
        if (!ec)
        {
            this_l->received_type ();
        }
        else
        {
            BOOST_LOG (this_l->connection->connection->client->log) << boost::str (boost::format ("Error receiving block type %1%") % ec.message ());
        }
    });
}

void rai::bulk_pull_client::received_type ()
{
    auto this_l (shared_from_this ());
    rai::block_type type (static_cast <rai::block_type> (receive_buffer [0]));
    switch (type)
    {
        case rai::block_type::send:
        {
            boost::asio::async_read (connection->connection->socket, boost::asio::buffer (receive_buffer.data () + 1, sizeof (rai::account) + sizeof (rai::block_hash) + sizeof (rai::amount) + sizeof (uint64_t) + sizeof (rai::signature)), [this_l] (boost::system::error_code const & ec, size_t size_a)
            {
                this_l->received_block (ec, size_a);
            });
            break;
        }
        case rai::block_type::receive:
        {
            boost::asio::async_read (connection->connection->socket, boost::asio::buffer (receive_buffer.data () + 1, sizeof (rai::block_hash) + sizeof (rai::block_hash) + sizeof (uint64_t) + sizeof (rai::signature)), [this_l] (boost::system::error_code const & ec, size_t size_a)
            {
                this_l->received_block (ec, size_a);
            });
            break;
        }
        case rai::block_type::open:
        {
            boost::asio::async_read (connection->connection->socket, boost::asio::buffer (receive_buffer.data () + 1, sizeof (rai::account) + sizeof (rai::block_hash) + sizeof (uint64_t) + sizeof (rai::signature)), [this_l] (boost::system::error_code const & ec, size_t size_a)
            {
                this_l->received_block (ec, size_a);
            });
            break;
        }
        case rai::block_type::change:
        {
            boost::asio::async_read (connection->connection->socket, boost::asio::buffer (receive_buffer.data () + 1, sizeof (rai::account) + sizeof (rai::block_hash) + sizeof (uint64_t) + sizeof (rai::signature)), [this_l] (boost::system::error_code const & ec, size_t size_a)
            {
                this_l->received_block (ec, size_a);
            });
            break;
        }
        case rai::block_type::not_a_block:
        {
            request ();
            break;
        }
        default:
        {
            BOOST_LOG (connection->connection->client->log) << "Unknown type received as block type";
            break;
        }
    }
}

rai::block_synchronization::block_synchronization (std::function <void (rai::block const &)> const & target_a, rai::block_store & store_a) :
target (target_a),
store (store_a)
{
}

rai::block_synchronization::~block_synchronization ()
{
}

namespace {
class add_dependency_visitor : public rai::block_visitor
{
public:
    add_dependency_visitor (rai::block_synchronization & sync_a) :
    sync (sync_a),
    result (true)
    {
    }
    void send_block (rai::send_block const & block_a) override
    {
        add_dependency (block_a.hashables.previous);
    }
    void receive_block (rai::receive_block const & block_a) override
    {
        add_dependency (block_a.hashables.previous);
        if (result)
        {
            add_dependency (block_a.hashables.source);
        }
    }
    void open_block (rai::open_block const & block_a) override
    {
        add_dependency (block_a.hashables.source);
    }
    void change_block (rai::change_block const & block_a) override
    {
        add_dependency (block_a.hashables.previous);
    }
    void add_dependency (rai::block_hash const & hash_a)
    {
        if (!sync.synchronized (hash_a))
        {
            result = false;
            sync.blocks.push (hash_a);
        }
    }
    rai::block_synchronization & sync;
    bool result;
};
}

bool rai::block_synchronization::add_dependency (rai::block const & block_a)
{
    add_dependency_visitor visitor (*this);
    block_a.visit (visitor);
    return visitor.result;
}

bool rai::block_synchronization::synchronize (rai::block_hash const & hash_a)
{
    auto result (false);
    blocks.push (hash_a);
    while (!result && !blocks.empty ())
    {
        auto block (retrieve (blocks.top ()));
        if (block != nullptr)
        {
            if (add_dependency (*block))
            {
                target (*block);
                blocks.pop ();
            }
            else
            {
                // Dependency was added to 'blocks'
            }
        }
        else
        {
            result = true;
        }
    }
    return result;
}

rai::pull_synchronization::pull_synchronization (std::function <void (rai::block const &)> const & target_a, rai::block_store & store_a) :
block_synchronization (target_a, store_a)
{
}

std::unique_ptr <rai::block> rai::pull_synchronization::retrieve (rai::block_hash const & hash_a)
{
    return store.bootstrap_get (hash_a);
}

bool rai::pull_synchronization::synchronized (rai::block_hash const & hash_a)
{
    return store.block_exists (hash_a);
}

rai::push_synchronization::push_synchronization (std::function <void (rai::block const &)> const & target_a, rai::block_store & store_a) :
block_synchronization (target_a, store_a)
{
}

bool rai::push_synchronization::synchronized (rai::block_hash const & hash_a)
{
    return sends.find (hash_a) == sends.end ();
}

std::unique_ptr <rai::block> rai::push_synchronization::retrieve (rai::block_hash const & hash_a)
{
    return store.block_get (hash_a);
}

rai::block_path::block_path (std::vector <std::unique_ptr <rai::block>> & path_a, std::function <std::unique_ptr <rai::block> (rai::block_hash const &)> const & retrieve_a) :
path (path_a),
retrieve (retrieve_a)
{
}

void rai::block_path::send_block (rai::send_block const & block_a)
{
    auto block (retrieve (block_a.hashables.previous));
    if (block != nullptr)
    {
        path.push_back (std::move (block));
    }
}

void rai::block_path::receive_block (rai::receive_block const & block_a)
{
	rai::block_path path_l (path, retrieve);
	path_l.generate (block_a.hashables.source);
    auto block (retrieve (block_a.hashables.previous));
    if (block != nullptr)
	{
        path.push_back (std::move (block));
	}
}

void rai::block_path::open_block (rai::open_block const & block_a)
{
    auto block (retrieve (block_a.hashables.source));
    if (block != nullptr)
    {
        path.push_back (std::move (block));
    }
}

void rai::block_path::change_block (rai::change_block const & block_a)
{
    auto block (retrieve (block_a.hashables.previous));
    if (block != nullptr)
    {
        path.push_back (std::move (block));
    }
}

void rai::block_path::generate (rai::block_hash const & hash_a)
{
    auto block (retrieve (hash_a));
	if (block != nullptr)
	{
		path.push_back (std::move (block));
		auto previous_size (0);
		while (previous_size != path.size ())
		{
			previous_size = path.size ();
			path.back ()->visit (*this);
		}
	}
}

void rai::bulk_pull_client::process_end ()
{
    std::vector <std::unique_ptr <rai::block>> path;
    while (connection->connection->client->store.bootstrap_begin () != connection->connection->client->store.bootstrap_end ())
    {
        path.clear ();
        rai::block_path filler (path, [this] (rai::block_hash const & hash_a)
        {
            std::unique_ptr <rai::block> result;
            auto block (connection->connection->client->store.bootstrap_get (hash_a));
            result = std::move (block);
            return result;
        });
        filler.generate (connection->connection->client->store.bootstrap_begin ()->first);
        while (!path.empty ())
        {
            auto hash (path.back ()->hash ());
            auto process_result (connection->connection->client->processor.process_receive (*path.back ()));
            switch (process_result)
            {
                case rai::process_result::progress:
                case rai::process_result::old:
                    break;
                default:
                    BOOST_LOG (connection->connection->client->log) << "Error inserting block";
                    break;
            }
            path.pop_back ();
            connection->connection->client->store.bootstrap_del (hash);
        }
    }
}

void rai::bulk_pull_client::received_block (boost::system::error_code const & ec, size_t size_a)
{
	if (!ec)
	{
		rai::bufferstream stream (receive_buffer.data (), 1 + size_a);
		auto block (rai::deserialize_block (stream));
		if (block != nullptr)
		{
            auto hash (block->hash ());
            if (bulk_pull_logging ())
            {
                std::string block_l;
                block->serialize_json (block_l);
                BOOST_LOG (connection->connection->client->log) << boost::str (boost::format ("Pulled block %1% %2%") % hash.to_string () % block_l);
            }
            connection->connection->client->store.bootstrap_put (hash, *block);
            receive_block ();
		}
        else
        {
            BOOST_LOG (connection->connection->client->log) << "Error deserializing block received from pull request";
        }
	}
}

rai::endpoint rai::network::endpoint ()
{
    return rai::endpoint (boost::asio::ip::address_v6::loopback (), socket.local_endpoint ().port ());
}

boost::asio::ip::tcp::endpoint rai::bootstrap_listener::endpoint ()
{
    return boost::asio::ip::tcp::endpoint (boost::asio::ip::address_v6::loopback (), local.port ());
}

rai::bootstrap_server::~bootstrap_server ()
{
    if (network_logging ())
    {
        BOOST_LOG (client->log) << "Exiting bootstrap connection";
    }
}

void rai::peer_container::random_fill (std::array <rai::endpoint, 8> & target_a)
{
    auto peers (list ());
    while (peers.size () > target_a.size ())
    {
        auto index (random_pool.GenerateWord32 (0, peers.size () - 1));
        assert (index < peers.size ());
        assert (index >= 0);
        peers [index] = peers [peers.size () - 1];
        peers.pop_back ();
    }
    assert (peers.size () <= target_a.size ());
    auto endpoint (rai::endpoint (boost::asio::ip::address_v6 {}, 0));
    assert (endpoint.address ().is_v6 ());
    std::fill (target_a.begin (), target_a.end (), endpoint);
    auto j (target_a.begin ());
    for (auto i (peers.begin ()), n (peers.end ()); i != n; ++i, ++j)
    {
        assert (i->endpoint.address ().is_v6 ());
        assert (j < target_a.end ());
        *j = i->endpoint;
    }
}

void rai::processor::ongoing_keepalive ()
{
    connect_bootstrap (client.bootstrap_peers);
    auto peers (client.peers.purge_list (std::chrono::system_clock::now () - cutoff));
    for (auto i (peers.begin ()), j (peers.end ()); i != j && std::chrono::system_clock::now () - i->last_attempt > period; ++i)
    {
        client.network.send_keepalive (i->endpoint);
    }
    client.service.add (std::chrono::system_clock::now () + period, [this] () { ongoing_keepalive ();});
}

std::vector <rai::peer_information> rai::peer_container::purge_list (std::chrono::system_clock::time_point const & cutoff)
{
    std::lock_guard <std::mutex> lock (mutex);
    auto pivot (peers.get <1> ().lower_bound (cutoff));
    std::vector <rai::peer_information> result (pivot, peers.get <1> ().end ());
    peers.get <1> ().erase (peers.get <1> ().begin (), pivot);
    for (auto i (peers.begin ()), n (peers.end ()); i != n; ++i)
    {
        peers.modify (i, [] (rai::peer_information & info) {info.last_attempt = std::chrono::system_clock::now ();});
    }
    return result;
}

size_t rai::peer_container::size ()
{
    std::lock_guard <std::mutex> lock (mutex);
    return peers.size ();
}

bool rai::peer_container::empty ()
{
    return size () == 0;
}

bool rai::peer_container::not_a_peer (rai::endpoint const & endpoint_a)
{
    bool result (false);
    if (endpoint_a.address ().to_v6 ().is_unspecified ())
    {
        result = true;
    }
    else if (rai::reserved_address (endpoint_a))
    {
        result = true;
    }
    else if (endpoint_a == self)
    {
        result = true;
    }
    return result;
}

bool rai::peer_container::insert (rai::endpoint const & endpoint_a)
{
    return insert (endpoint_a, rai::block_hash (0));
}

bool rai::peer_container::knows_about (rai::endpoint const & endpoint_a, rai::block_hash const & hash_a)
{
    std::lock_guard <std::mutex> lock (mutex);
    bool result (false);
    auto existing (peers.find (endpoint_a));
    if (existing != peers.end ())
    {
        result = existing->most_recent == hash_a;
    }
    return result;
}

bool rai::peer_container::insert (rai::endpoint const & endpoint_a, rai::block_hash const & hash_a)
{
	auto unknown (false);
    auto result (not_a_peer (endpoint_a));
    if (!result)
    {
        std::lock_guard <std::mutex> lock (mutex);
        auto existing (peers.find (endpoint_a));
        if (existing != peers.end ())
        {
            peers.modify (existing, [&hash_a] (rai::peer_information & info)
            {
                info.last_contact = std::chrono::system_clock::now ();
                info.most_recent = hash_a;
            });
            result = true;
        }
        else
        {
            peers.insert ({endpoint_a, std::chrono::system_clock::now (), std::chrono::system_clock::now (), hash_a});
			unknown = true;
        }
    }
	if (unknown)
	{
		peer_observer (endpoint_a);
	}
    return result;
}

namespace {
boost::asio::ip::address_v6 mapped_from_v4_bytes (unsigned long address_a)
{
    return boost::asio::ip::address_v6::v4_mapped (boost::asio::ip::address_v4 (address_a));
}
}

bool rai::reserved_address (rai::endpoint const & endpoint_a)
{
    assert (endpoint_a.address ().is_v6 ());
	auto bytes (endpoint_a.address ().to_v6 ());
	auto result (false);
    if (bytes >= mapped_from_v4_bytes (0x00000000ul) && bytes <= mapped_from_v4_bytes (0x00fffffful)) // Broadcast RFC1700
	{
		result = true;
	}
	else if (bytes >= mapped_from_v4_bytes (0xc0000200ul) && bytes <= mapped_from_v4_bytes (0xc00002fful)) // TEST-NET RFC5737
	{
		result = true;
	}
	else if (bytes >= mapped_from_v4_bytes (0xc6336400ul) && bytes <= mapped_from_v4_bytes (0xc63364fful)) // TEST-NET-2 RFC5737
	{
		result = true;
	}
	else if (bytes >= mapped_from_v4_bytes (0xcb007100ul) && bytes <= mapped_from_v4_bytes (0xcb0071fful)) // TEST-NET-3 RFC5737
	{
		result = true;
	}
	else if (bytes >= mapped_from_v4_bytes (0xe9fc0000ul) && bytes <= mapped_from_v4_bytes (0xe9fc00fful))
	{
		result = true;
	}
	else if (bytes >= mapped_from_v4_bytes (0xf0000000ul)) // Reserved RFC6890
	{
		result = true;
	}
	return result;
}

rai::peer_container::peer_container (rai::endpoint const & self_a) :
self (self_a),
peer_observer ([] (rai::endpoint const &) {})
{
}

std::ostream & operator << (std::ostream & stream_a, std::chrono::system_clock::time_point const & time_a)
{
    time_t last_contact (std::chrono::system_clock::to_time_t (time_a));
    std::string string (ctime (&last_contact));
    string.pop_back ();
    stream_a << string;
    return stream_a;
}

void rai::network::send_buffer (uint8_t const * data_a, size_t size_a, rai::endpoint const & endpoint_a, std::function <void (boost::system::error_code const &, size_t)> callback_a)
{
    std::unique_lock <std::mutex> lock (socket_mutex);
    auto do_send (sends.empty ());
    sends.push (std::make_tuple (data_a, size_a, endpoint_a, callback_a));
    if (do_send)
    {
        if (network_packet_logging ())
        {
            BOOST_LOG (client.log) << "Sending packet";
        }
        socket.async_send_to (boost::asio::buffer (data_a, size_a), endpoint_a, [this] (boost::system::error_code const & ec, size_t size_a)
        {
            send_complete (ec, size_a);
        });
    }
}

void rai::network::send_complete (boost::system::error_code const & ec, size_t size_a)
{
    if (network_packet_logging ())
    {
        BOOST_LOG (client.log) << "Packet send complete";
    }
    std::tuple <uint8_t const *, size_t, rai::endpoint, std::function <void (boost::system::error_code const &, size_t)>> self;
    {
        std::unique_lock <std::mutex> lock (socket_mutex);
        assert (!sends.empty ());
        self = sends.front ();
        sends.pop ();
        if (!sends.empty ())
        {
            auto & front (sends.front ());
            if (network_packet_logging ())
            {
                if (network_packet_logging ())
                {
                    BOOST_LOG (client.log) << "Sending packet";
                }
            }
            socket.async_send_to (boost::asio::buffer (std::get <0> (front), std::get <1> (front)), std::get <2> (front), [this] (boost::system::error_code const & ec, size_t size_a)
            {
                send_complete (ec, size_a);
            });
        }
    }
    std::get <3> (self) (ec, size_a);
}

uint64_t rai::block_store::now ()
{
    boost::posix_time::ptime epoch (boost::gregorian::date (1970, 1, 1));
    auto now (boost::posix_time::second_clock::universal_time ());
    auto diff (now - epoch);
    return diff.total_seconds ();
}

rai::bulk_push_server::bulk_push_server (std::shared_ptr <rai::bootstrap_server> const & connection_a) :
connection (connection_a)
{
}

void rai::bulk_push_server::receive ()
{
    auto this_l (shared_from_this ());
    boost::asio::async_read (*connection->socket, boost::asio::buffer (receive_buffer.data (), 1), [this_l] (boost::system::error_code const & ec, size_t size_a)
        {
            if (!ec)
            {
                this_l->received_type ();
            }
            else
            {
                BOOST_LOG (this_l->connection->client->log) << boost::str (boost::format ("Error receiving block type %1%") % ec.message ());
            }
        });
}

void rai::bulk_push_server::received_type ()
{
    auto this_l (shared_from_this ());
    rai::block_type type (static_cast <rai::block_type> (receive_buffer [0]));
    switch (type)
    {
        case rai::block_type::send:
        {
            boost::asio::async_read (*connection->socket, boost::asio::buffer (receive_buffer.data () + 1, sizeof (rai::account) + sizeof (rai::block_hash) + sizeof (rai::amount) + sizeof (uint64_t) + sizeof (rai::signature)), [this_l] (boost::system::error_code const & ec, size_t size_a)
                                     {
                                         this_l->received_block (ec, size_a);
                                     });
            break;
        }
        case rai::block_type::receive:
        {
            boost::asio::async_read (*connection->socket, boost::asio::buffer (receive_buffer.data () + 1, sizeof (rai::block_hash) + sizeof (rai::block_hash) + sizeof (uint64_t) + sizeof (rai::signature)), [this_l] (boost::system::error_code const & ec, size_t size_a)
                                     {
                                         this_l->received_block (ec, size_a);
                                     });
            break;
        }
        case rai::block_type::open:
        {
            boost::asio::async_read (*connection->socket, boost::asio::buffer (receive_buffer.data () + 1, sizeof (rai::account) + sizeof (rai::block_hash) + sizeof (uint64_t) + sizeof (rai::signature)), [this_l] (boost::system::error_code const & ec, size_t size_a)
                                     {
                                         this_l->received_block (ec, size_a);
                                     });
            break;
        }
        case rai::block_type::change:
        {
            boost::asio::async_read (*connection->socket, boost::asio::buffer (receive_buffer.data () + 1, sizeof (rai::account) + sizeof (rai::block_hash) + sizeof (uint64_t) + sizeof (rai::signature)), [this_l] (boost::system::error_code const & ec, size_t size_a)
                                     {
                                         this_l->received_block (ec, size_a);
                                     });
            break;
        }
        case rai::block_type::not_a_block:
        {
            connection->finish_request ();
            break;
        }
        default:
        {
            BOOST_LOG (connection->client->log) << "Unknown type received as block type";
            break;
        }
    }
}

void rai::bulk_push_server::received_block (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
        rai::bufferstream stream (receive_buffer.data (), 1 + size_a);
        auto block (rai::deserialize_block (stream));
        if (block != nullptr)
        {
            connection->client->processor.process_receive_republish (std::move (block));
            receive ();
        }
        else
        {
            BOOST_LOG (connection->client->log) << "Error deserializing block received from pull request";
        }
    }
}

rai::bulk_pull_server::bulk_pull_server (std::shared_ptr <rai::bootstrap_server> const & connection_a, std::unique_ptr <rai::bulk_pull> request_a) :
connection (connection_a),
request (std::move (request_a))
{
    set_current_end ();
}

rai::frontier_req_server::frontier_req_server (std::shared_ptr <rai::bootstrap_server> const & connection_a, std::unique_ptr <rai::frontier_req> request_a) :
connection (connection_a),
iterator (connection_a->client->store.latest_begin (request_a->start)),
request (std::move (request_a))
{
    skip_old ();
}

void rai::frontier_req_server::skip_old ()
{
    if (request->age != std::numeric_limits<decltype (request->age)>::max ())
    {
        auto now (connection->client->store.now ());
        while (iterator != connection->client->ledger.store.latest_end () && (now - iterator->second.time) >= request->age)
        {
            ++iterator;
        }
    }
}

void rai::frontier_req_server::send_next ()
{
	auto pair (get_next ());
    if (!pair.first.is_zero ())
    {
        {
            send_buffer.clear ();
            rai::vectorstream stream (send_buffer);
            write (stream, pair.first.bytes);
            write (stream, pair.second.bytes);
        }
        auto this_l (shared_from_this ());
        if (network_logging ())
        {
            BOOST_LOG (connection->client->log) << boost::str (boost::format ("Sending frontier for %1% %2%") % pair.first.to_string () % pair.second.to_string ());
        }
        async_write (*connection->socket, boost::asio::buffer (send_buffer.data (), send_buffer.size ()), [this_l] (boost::system::error_code const & ec, size_t size_a)
        {
            this_l->sent_action (ec, size_a);
        });
    }
    else
    {
        send_finished ();
    }
}

void rai::frontier_req_server::send_finished ()
{
    {
        send_buffer.clear ();
        rai::vectorstream stream (send_buffer);
        rai::uint256_union zero (0);
        write (stream, zero.bytes);
        write (stream, zero.bytes);
    }
    auto this_l (shared_from_this ());
    if (network_logging ())
    {
        BOOST_LOG (connection->client->log) << "Frontier sending finished";
    }
    async_write (*connection->socket, boost::asio::buffer (send_buffer.data (), send_buffer.size ()), [this_l] (boost::system::error_code const & ec, size_t size_a)
    {
        this_l->no_block_sent (ec, size_a);
    });
}

void rai::frontier_req_server::no_block_sent (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
		connection->finish_request ();
    }
    else
    {
        if (network_logging ())
        {
            BOOST_LOG (connection->client->log) << boost::str (boost::format ("Error sending frontier finish %1%") % ec.message ());
        }
    }
}

void rai::frontier_req_server::sent_action (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
        send_next ();
    }
    else
    {
        if (network_logging ())
        {
            BOOST_LOG (connection->client->log) << boost::str (boost::format ("Error sending frontier pair %1%") % ec.message ());
        }
    }
}

std::pair <rai::uint256_union, rai::uint256_union> rai::frontier_req_server::get_next ()
{
    std::pair <rai::uint256_union, rai::uint256_union> result (0, 0);
    if (iterator != connection->client->ledger.store.latest_end ())
    {
        result.first = iterator->first;
        result.second = iterator->second.hash;
        ++iterator;
    }
    return result;
}

rai::frontier_req::frontier_req () :
message (rai::message_type::frontier_req)
{
}

bool rai::frontier_req::deserialize (rai::stream & stream_a)
{
	auto result (read_header (stream_a, version_max, version_using, version_min, type, extensions));
	assert (!result);
	assert (rai::message_type::frontier_req == type);
    if (!result)
    {
        assert (type == rai::message_type::frontier_req);
        result = read (stream_a, start.bytes);
        if (!result)
        {
            result = read (stream_a, age);
            if (!result)
            {
                result = read (stream_a, count);
            }
        }
    }
    return result;
}

void rai::frontier_req::serialize (rai::stream & stream_a)
{
	write_header (stream_a);
    write (stream_a, start.bytes);
    write (stream_a, age);
    write (stream_a, count);
}

void rai::frontier_req::visit (rai::message_visitor & visitor_a) const
{
    visitor_a.frontier_req (*this);
}

bool rai::frontier_req::operator == (rai::frontier_req const & other_a) const
{
    return start == other_a.start && age == other_a.age && count == other_a.count;
}

rai::bulk_pull_client::bulk_pull_client (std::shared_ptr <rai::frontier_req_client> const & connection_a) :
connection (connection_a),
current (connection->pulls.begin ()),
end (connection->pulls.end ())
{
}

rai::bulk_pull_client::~bulk_pull_client ()
{
    if (network_logging ())
    {
        BOOST_LOG (connection->connection->client->log) << "Exiting bulk pull client";
    }
}

rai::frontier_req_client::frontier_req_client (std::shared_ptr <rai::bootstrap_client> const & connection_a) :
connection (connection_a),
current (connection->client->store.latest_begin ()),
end (connection->client->store.latest_end ())
{
}

rai::frontier_req_client::~frontier_req_client ()
{
    if (network_logging ())
    {
        BOOST_LOG (connection->client->log) << "Exiting frontier_req initiator";
    }
}

void rai::frontier_req_client::receive_frontier ()
{
    auto this_l (shared_from_this ());
    boost::asio::async_read (connection->socket, boost::asio::buffer (receive_buffer.data (), sizeof (rai::uint256_union) + sizeof (rai::uint256_union)), [this_l] (boost::system::error_code const & ec, size_t size_a)
    {
        this_l->received_frontier (ec, size_a);
    });
}

void rai::frontier_req_client::request_account (rai::account const & account_a)
{
    // Account they know about and we don't.
    pulls [account_a] = rai::block_hash (0);
}

void rai::frontier_req_client::completed_pulls ()
{
    auto this_l (shared_from_this ());
    auto pushes (std::make_shared <rai::bulk_push_client> (this_l));
    pushes->start ();
}

void rai::frontier_req_client::received_frontier (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
        assert (size_a == sizeof (rai::uint256_union) + sizeof (rai::uint256_union));
        rai::account account;
        rai::bufferstream account_stream (receive_buffer.data (), sizeof (rai::uint256_union));
        auto error1 (rai::read (account_stream, account));
        assert (!error1);
        rai::block_hash latest;
        rai::bufferstream latest_stream (receive_buffer.data () + sizeof (rai::uint256_union), sizeof (rai::uint256_union));
        auto error2 (rai::read (latest_stream, latest));
        assert (!error2);
        if (!account.is_zero ())
        {
            while (current != end && current->first < account)
            {
                // We know about an account they don't.
                pushes [current->first] = rai::block_hash (0);
                ++current;
            }
            if (current != end)
            {
                if (account == current->first)
                {
                    if (latest == current->second.hash)
                    {
                        // In sync
                    }
                    else if (connection->client->store.block_exists (latest))
                    {
                        // We know about a block they don't.
                        pushes [account] = latest;
                    }
                    else
                    {
                        // They know about a block we don't.
                        pulls [account] = current->second.hash;
                    }
                    ++current;
                }
                else
                {
                    assert (account < current->first);
                    request_account (account);
                }
            }
            else
            {
                request_account (account);
            }
            receive_frontier ();
        }
        else
        {
            while (current != end)
            {
                // We know about an account they don't.
                pushes [current->first] = rai::block_hash (0);
                ++current;
            }
            completed_requests ();
        }
    }
    else
    {
        if (network_logging ())
        {
            BOOST_LOG (connection->client->log) << boost::str (boost::format ("Error while receiving frontier %1%") % ec.message ());
        }
    }
}

void rai::frontier_req_client::completed_requests ()
{
    auto this_l (shared_from_this ());
    auto pulls (std::make_shared <rai::bulk_pull_client> (this_l));
    pulls->request ();
}

void rai::frontier_req_client::completed_pushes ()
{
}

rai::bulk_push_client::bulk_push_client (std::shared_ptr <rai::frontier_req_client> const & connection_a) :
connection (connection_a),
current (connection->pushes.begin ()),
end (connection->pushes.end ())
{
}

rai::bulk_push_client::~bulk_push_client ()
{
    if (network_logging ())
    {
        BOOST_LOG (connection->connection->client->log) << "Exiting bulk push client";
    }
}

void rai::bulk_push_client::start ()
{
    rai::bulk_push message;
    auto buffer (std::make_shared <std::vector <uint8_t>> ());
    {
        rai::vectorstream stream (*buffer);
        message.serialize (stream);
    }
    auto this_l (shared_from_this ());
    boost::asio::async_write (connection->connection->socket, boost::asio::buffer (buffer->data (), buffer->size ()), [this_l, buffer] (boost::system::error_code const & ec, size_t size_a)
        {
            if (!ec)
            {
                this_l->push ();
            }
            else
            {
                BOOST_LOG (this_l->connection->connection->client->log) << boost::str (boost::format ("Unable to send bulk_push request %1%") % ec.message ());
            }
        });
}

void rai::bulk_push_client::push ()
{
    if (current != end)
    {
        path.clear ();
        rai::block_path filler (path, [this] (rai::block_hash const & hash_a)
        {
            std::unique_ptr <rai::block> result;
            auto block (connection->connection->client->store.block_get (hash_a));
            if (block != nullptr)
            {
                result = std::move (block);
            }
            return result;
        });
        auto hash (current->first);
		rai::frontier frontier;
		auto error (connection->connection->client->store.latest_get (hash, frontier));
        assert (!error);
        ++current;
        filler.generate (frontier.hash);
        push_block ();
    }
    else
    {
        send_finished ();
    }
}

void rai::bulk_push_client::send_finished ()
{
    auto buffer (std::make_shared <std::vector <uint8_t>> ());
    buffer->push_back (static_cast <uint8_t> (rai::block_type::not_a_block));
    if (network_logging ())
    {
        BOOST_LOG (connection->connection->client->log) << "Bulk push finished";
    }
    auto this_l (shared_from_this ());
    async_write (connection->connection->socket, boost::asio::buffer (buffer->data (), 1), [this_l] (boost::system::error_code const & ec, size_t size_a)
        {
            this_l->connection->completed_pushes ();
        });
}

void rai::bulk_push_client::push_block ()
{
    assert (!path.empty ());
    auto buffer (std::make_shared <std::vector <uint8_t>> ());
    {
        rai::vectorstream stream (*buffer);
        rai::serialize_block (stream, *path.back ());
    }
    path.pop_back ();
    auto this_l (shared_from_this ());
    boost::asio::async_write (connection->connection->socket, boost::asio::buffer (buffer->data (), buffer->size ()), [this_l] (boost::system::error_code const & ec, size_t size_a)
        {
            if (!ec)
            {
                if (!this_l->path.empty ())
                {
                    this_l->push_block ();
                }
                else
                {
                    this_l->push ();
                }
            }
            else
            {
                BOOST_LOG (this_l->connection->connection->client->log) << boost::str (boost::format ("Error sending block during bulk push %1%") % ec.message ());
            }
        });
}

bool rai::keepalive::operator == (rai::keepalive const & other_a) const
{
	return peers == other_a.peers;
}

bool rai::peer_container::known_peer (rai::endpoint const & endpoint_a)
{
    std::lock_guard <std::mutex> lock (mutex);
    auto existing (peers.find (endpoint_a));
    return existing != peers.end () && existing->last_contact > std::chrono::system_clock::now () - rai::processor::cutoff;
}

std::shared_ptr <rai::client> rai::client::shared ()
{
    return shared_from_this ();
}

namespace
{
class traffic_generator : public std::enable_shared_from_this <traffic_generator>
{
public:
    traffic_generator (uint32_t count_a, uint32_t wait_a, std::shared_ptr <rai::client> client_a, rai::system & system_a) :
    count (count_a),
    wait (wait_a),
    client (client_a),
    system (system_a)
    {
    }
    void run ()
    {
        auto count_l (count - 1);
        count = count_l - 1;
        system.generate_activity (*client);
        if (count_l > 0)
        {
            auto this_l (shared_from_this ());
            client->service.add (std::chrono::system_clock::now () + std::chrono::milliseconds (wait), [this_l] () {this_l->run ();});
        }
    }
    uint32_t count;
    uint32_t wait;
    std::shared_ptr <rai::client> client;
    rai::system & system;
};
}

void rai::system::generate_usage_traffic (uint32_t count_a, uint32_t wait_a)
{
    for (size_t i (0), n (clients.size ()); i != n; ++i)
    {
        generate_usage_traffic (count_a, wait_a, i);
    }
}

void rai::system::generate_usage_traffic (uint32_t count_a, uint32_t wait_a, size_t index_a)
{
    assert (clients.size () > index_a);
    assert (count_a > 0);
    auto generate (std::make_shared <traffic_generator> (count_a, wait_a, clients [index_a], *this));
    generate->run ();
}

void rai::system::generate_activity (rai::client & client_a)
{
    auto what (random_pool.GenerateByte ());
    if (what < 0xc0 && client_a.store.latest_begin () != client_a.store.latest_end ())
    {
        generate_send_existing (client_a);
    }
    else
    {
        generate_send_new (client_a);
    }
    size_t polled;
    do
    {
        polled = 0;
        polled += service->poll ();
        polled += processor.poll ();
    } while (polled != 0);
}

rai::uint128_t rai::system::get_random_amount (rai::client & client_a)
{
    rai::uint128_t balance (wallet (0)->store.balance (client_a.ledger));
    std::string balance_text (balance.convert_to <std::string> ());
    rai::uint128_union random_amount;
    random_pool.GenerateBlock (random_amount.bytes.data (), sizeof (random_amount.bytes));
    auto result (((rai::uint256_t {random_amount.number ()} * balance) / rai::uint256_t {std::numeric_limits <rai::uint128_t>::max ()}).convert_to <rai::uint128_t> ());
    std::string text (result.convert_to <std::string> ());
    return result;
}

void rai::system::generate_send_existing (rai::client & client_a)
{
    rai::account account;
    random_pool.GenerateBlock (account.bytes.data (), sizeof (account.bytes));
    rai::account_iterator entry (client_a.store.latest_begin (account));
    if (entry == client_a.store.latest_end ())
    {
        entry = client_a.store.latest_begin ();
    }
    assert (entry != client_a.store.latest_end ());
    wallet (0)->send (entry->first, get_random_amount (client_a));
}

void rai::system::generate_send_new (rai::client & client_a)
{
    assert (client_a.wallets.items.size () == 1);
    rai::keypair key;
    client_a.wallets.items.begin ()->second->store.insert (key.prv);
    client_a.wallets.items.begin ()->second->send (key.pub, get_random_amount (client_a));
}

void rai::system::generate_mass_activity (uint32_t count_a, rai::client & client_a)
{
    auto previous (std::chrono::system_clock::now ());
    for (uint32_t i (0); i < count_a; ++i)
    {
        if ((i & 0x3ff) == 0)
        {
            auto now (std::chrono::system_clock::now ());
            auto ms (std::chrono::duration_cast <std::chrono::milliseconds> (now - previous).count ());
            std::cerr << boost::str (boost::format ("Mass activity iteration %1% ms %2% ms/t %3%\n") % i % ms % (ms / 256));
            previous = now;
        }
        generate_activity (client_a);
    }
}

rai::uint128_t rai::wallet_store::balance (rai::ledger & ledger_a)
{
    rai::uint128_t result;
    for (auto i (begin ()), n (end ()); i !=  n; ++i)
    {
        auto pub (i->first);
        auto account_balance (ledger_a.account_balance (pub));
        result += account_balance;
    }
    return result;
}

rai::election::election (std::shared_ptr <rai::client> client_a, rai::block const & block_a) :
votes (block_a.root ()),
client (client_a),
last_vote (std::chrono::system_clock::now ()),
last_winner (block_a.clone ()),
confirmed (false)
{
    assert (client_a->store.block_exists (block_a.hash ()));
    rai::keypair anonymous;
    rai::vote vote_l;
    vote_l.account = anonymous.pub;
    vote_l.sequence = 0;
    vote_l.block = block_a.clone ();
    rai::sign_message (anonymous.prv, anonymous.pub, vote_l.hash (), vote_l.signature);
    vote (vote_l);
}

void rai::election::start ()
{
	auto client_l (client.lock ());
	if (client_l != nullptr)
	{
		auto have_representative (client_l->representative_vote (*this, *last_winner));
		if (have_representative)
		{
			announce_vote ();
		}
		timeout_action ();
	}
}

void rai::election::timeout_action ()
{
	auto client_l (client.lock ());
	if (client_l != nullptr)
	{
		auto now (std::chrono::system_clock::now ());
		if (now - last_vote < std::chrono::seconds (15))
		{
			auto this_l (shared_from_this ());
			client_l->service.add (now + std::chrono::seconds (15), [this_l] () {this_l->timeout_action ();});
		}
		else
		{
			auto root_l (votes.id);
			client_l->conflicts.stop (root_l);
		}
	}
}

rai::uint128_t rai::election::uncontested_threshold (rai::ledger & ledger_a)
{
    return ledger_a.supply () / 2;
}

rai::uint128_t rai::election::contested_threshold (rai::ledger & ledger_a)
{
    return (ledger_a.supply () / 16) * 15;
}

void rai::election::vote (rai::vote const & vote_a)
{
	auto client_l (client.lock ());
	if (client_l != nullptr)
	{
		auto changed (votes.vote (vote_a));
		if (!confirmed && changed)
		{
			auto tally_l (client_l->ledger.tally (votes));
			assert (tally_l.size () > 0);
			auto winner (tally_l.begin ()->second->clone ());
			if (!(*winner == *last_winner))
			{
				client_l->ledger.rollback (last_winner->hash ());
				client_l->ledger.process (*winner);
				last_winner = std::move (winner);
			}
			if (tally_l.size () == 1)
			{
				if (tally_l.begin ()->first > uncontested_threshold (client_l->ledger))
				{
					confirmed = true;
					client_l->processor.process_confirmed (*last_winner);
				}
			}
			else
			{
				if (tally_l.begin ()->first > contested_threshold (client_l->ledger))
				{
					confirmed = true;
					client_l->processor.process_confirmed (*last_winner);
				}
			}
		}
	}
}

void rai::election::start_request (rai::block const & block_a)
{
	auto client_l (client.lock ());
	if (client_l != nullptr)
	{
		auto list (client_l->peers.list ());
		for (auto i (list.begin ()), j (list.end ()); i != j; ++i)
		{
			client_l->network.send_confirm_req (i->endpoint, block_a);
		}
	}
}

void rai::election::announce_vote ()
{
	auto client_l (client.lock ());
	if (client_l != nullptr)
	{
		auto winner_l (client_l->ledger.winner (votes));
		assert (winner_l.second != nullptr);
		auto list (client_l->peers.list ());
		client_l->network.confirm_broadcast (list, std::move (winner_l.second), votes.sequence);
		auto now (std::chrono::system_clock::now ());
		if (now - last_vote < std::chrono::seconds (15))
		{
			auto this_l (shared_from_this ());
			client_l->service.add (now + std::chrono::seconds (15), [this_l] () {this_l->announce_vote ();});
		}
	}
}

void rai::conflicts::start (rai::block const & block_a, bool request_a)
{
    std::lock_guard <std::mutex> lock (mutex);
    auto root (block_a.root ());
    auto existing (roots.find (root));
    if (existing == roots.end ())
    {
        auto election (std::make_shared <rai::election> (client.shared (), block_a));
		client.service.add (std::chrono::system_clock::now (), [election] () {election->start ();});
        roots.insert (std::make_pair (root, election));
        if (request_a)
        {
            election->start_request (block_a);
        }
    }
}

bool rai::conflicts::no_conflict (rai::block_hash const & hash_a)
{
    std::lock_guard <std::mutex> lock (mutex);
    auto result (true);
    auto existing (roots.find (hash_a));
    if (existing != roots.end ())
    {
        auto size (existing->second->votes.rep_votes.size ());
		if (size > 1)
		{
			auto & block (existing->second->votes.rep_votes.begin ()->second.second);
			for (auto i (existing->second->votes.rep_votes.begin ()), n (existing->second->votes.rep_votes.end ()); i != n && result; ++i)
			{
				result = *block == *i->second.second;
			}
		}
    }
    return result;
}

// Validate a vote and apply it to the current election or start a new election if it doesn't exist
void rai::conflicts::update (rai::vote const & vote_a)
{
    std::lock_guard <std::mutex> lock (mutex);
    auto existing (roots.find (vote_a.block->root ()));
    if (existing != roots.end ())
    {
        existing->second->vote (vote_a);
    }
}

void rai::conflicts::stop (rai::block_hash const & root_a)
{
    std::lock_guard <std::mutex> lock (mutex);
    assert (roots.find (root_a) != roots.end ());
    roots.erase (root_a);
}

rai::conflicts::conflicts (rai::client & client_a) :
client (client_a)
{
}

void rai::processor::process_message (rai::message & message_a, rai::endpoint const & sender_a)
{
	network_message_visitor visitor (client, sender_a);
	message_a.visit (visitor);
}

namespace
{
class confirmed_visitor : public rai::block_visitor
{
public:
    confirmed_visitor (rai::client & client_a) :
    client (client_a)
    {
    }
    void send_block (rai::send_block const & block_a) override
    {
        rai::private_key prv;
        for (auto i (client.wallets.items.begin ()), n (client.wallets.items.end ()); i != n; ++i)
        {
            if (!i->second->store.fetch (block_a.hashables.destination, prv))
            {
                auto error (i->second->receive (block_a, prv, i->second->store.representative ()));
                prv.clear ();
            }
            else
            {
                BOOST_LOG (client.log) << "While confirming, unable to fetch wallet key";
            }
        }
    }
    void receive_block (rai::receive_block const &) override
    {
    }
    void open_block (rai::open_block const &) override
    {
    }
    void change_block (rai::change_block const &) override
    {
    }
    rai::client & client;
};
}

void rai::processor::process_confirmed (rai::block const & confirmed_a)
{
    confirmed_visitor visitor (client);
    confirmed_a.visit (visitor);
}

bool rai::client::representative_vote (rai::election & election_a, rai::block const & block_a)
{
    bool result (false);
    for (auto i (wallets.items.begin ()), n (wallets.items.end ()); i != n; ++i)
	{
        if (i->second->store.is_representative ())
        {
            auto representative (i->second->store.representative ());
            rai::private_key prv;
            rai::vote vote_l;
            vote_l.account = representative;
            vote_l.sequence = 0;
            vote_l.block = block_a.clone ();
            i->second->store.fetch (representative, prv);
            rai::sign_message (prv, representative, vote_l.hash (), vote_l.signature);
            prv.clear ();
            election_a.vote (vote_l);
            result = true;
        }
	}
    return result;
}

rai::uint256_union rai::wallet_store::check ()
{
    std::string check;
    auto status (handle->Get (leveldb::ReadOptions (), leveldb::Slice (rai::wallet_store::check_special.chars.data (), rai::wallet_store::check_special.chars.size ()), &check));
    assert (status.ok ());
    rai::uint256_union result;
    assert (check.size () == sizeof (rai::uint256_union));
    std::copy (check.begin (), check.end (), result.chars.begin ());
    return result;
}

rai::uint256_union rai::wallet_store::salt ()
{
    std::string salt_string;
    auto status (handle->Get (leveldb::ReadOptions (), leveldb::Slice (rai::wallet_store::salt_special.chars.data (), rai::wallet_store::salt_special.chars.size ()), &salt_string));
    assert (status.ok ());
    rai::uint256_union result;
    assert (salt_string.size () == result.chars.size ());
    std::copy (salt_string.data (), salt_string.data () + salt_string.size (), result.chars.data ());
    return result;
}

rai::uint256_union rai::wallet_store::wallet_key ()
{
    std::string encrypted_wallet_key;
    auto status (handle->Get (leveldb::ReadOptions (), leveldb::Slice (rai::wallet_store::wallet_key_special.chars.data (), rai::wallet_store::wallet_key_special.chars.size ()), &encrypted_wallet_key));
    assert (status.ok ());
    assert (encrypted_wallet_key.size () == sizeof (rai::uint256_union));
    rai::uint256_union encrypted_key;
    std::copy (encrypted_wallet_key.begin (), encrypted_wallet_key.end (), encrypted_key.chars.begin ());
    auto password_l (password.value ());
    auto result (encrypted_key.prv (password_l, salt ().owords [0]));
    password_l.clear ();
    return result;
}

bool rai::wallet_store::valid_password ()
{
    rai::uint256_union zero;
    zero.clear ();
    auto wallet_key_l (wallet_key ());
    rai::uint256_union check_l (zero, wallet_key_l, salt ().owords [0]);
    wallet_key_l.clear ();
    return check () == check_l;
}

void rai::wallet_store::enter_password (std::string const & password_a)
{
    password.value_set (derive_key (password_a));
}

bool rai::wallet_store::rekey (std::string const & password_a)
{
    bool result (false);
	if (valid_password ())
    {
        auto password_new (derive_key (password_a));
        auto wallet_key_l (wallet_key ());
        auto password_l (password.value ());
        (*password.values [0]) ^= password_l;
        (*password.values [0]) ^= password_new;
        rai::uint256_union encrypted (wallet_key_l, password_new, salt ().owords [0]);
        auto status1 (handle->Put (leveldb::WriteOptions (), leveldb::Slice (rai::wallet_store::wallet_key_special.chars.data (), rai::wallet_store::wallet_key_special.chars.size ()), leveldb::Slice (encrypted.chars.data (), encrypted.chars.size ())));
        wallet_key_l.clear ();
        assert (status1.ok ());
    }
    else
    {
        result = true;
    }
    return result;
}

rai::uint256_union rai::wallet_store::derive_key (std::string const & password_a)
{
    rai::kdf kdf (kdf_work);
    auto result (kdf.generate (password_a, salt ()));
    return result;
}

bool rai::confirm_req::operator == (rai::confirm_req const & other_a) const
{
    return *block == *other_a.block;
}

bool rai::publish::operator == (rai::publish const & other_a) const
{
    return *block == *other_a.block;
}

rai::fan::fan (rai::uint256_union const & key, size_t count_a)
{
    std::unique_ptr <rai::uint256_union> first (new rai::uint256_union (key));
    for (auto i (0); i != count_a; ++i)
    {
        std::unique_ptr <rai::uint256_union> entry (new rai::uint256_union);
        random_pool.GenerateBlock (entry->bytes.data (), entry->bytes.size ());
        *first ^= *entry;
        values.push_back (std::move (entry));
    }
    values.push_back (std::move (first));
}

rai::uint256_union rai::fan::value ()
{
    rai::uint256_union result;
    result.clear ();
    for (auto & i: values)
    {
        result ^= *i;
    }
    return result;
}

void rai::fan::value_set (rai::uint256_union const & value_a)
{
    auto value_l (value ());
    *(values [0]) ^= value_l;
    *(values [0]) ^= value_a;
}

std::array <uint8_t, 2> constexpr rai::message::magic_number;
size_t constexpr rai::message::ipv4_only_position;
size_t constexpr rai::message::bootstrap_server_position;
std::bitset <16> constexpr rai::message::block_type_mask;

rai::message::message (rai::message_type type_a) :
version_max (0x01),
version_using (0x01),
version_min (0x01),
type (type_a)
{
}

rai::block_type rai::message::block_type () const
{
    return static_cast <rai::block_type> (((extensions & block_type_mask) >> 8).to_ullong ());
}

void rai::message::block_type_set (rai::block_type type_a)
{
    extensions &= ~rai::message::block_type_mask;
    extensions |= std::bitset <16> (static_cast <unsigned long long> (type_a) << 8);
}

bool rai::message::ipv4_only ()
{
    return extensions.test (ipv4_only_position);
}

void rai::message::ipv4_only_set (bool value_a)
{
    extensions.set (ipv4_only_position, value_a);
}

void rai::message::write_header (rai::stream & stream_a)
{
    rai::write (stream_a, rai::message::magic_number);
    rai::write (stream_a, version_max);
    rai::write (stream_a, version_using);
    rai::write (stream_a, version_min);
    rai::write (stream_a, type);
    rai::write (stream_a, static_cast <uint16_t> (extensions.to_ullong ()));
}

bool rai::message::read_header (rai::stream & stream_a, uint8_t & version_max_a, uint8_t & version_using_a, uint8_t & version_min_a, rai::message_type & type_a, std::bitset <16> & extensions_a)
{
    std::array <uint8_t, 2> magic_number_l;
    auto result (rai::read (stream_a, magic_number_l));
    if (!result)
    {
        result = magic_number_l != magic_number;
        if (!result)
        {
            result = rai::read (stream_a, version_max_a);
            if (!result)
            {
                result = rai::read (stream_a, version_using_a);
                if (!result)
                {
                    result = rai::read (stream_a, version_min_a);
                    if (!result)
                    {
                        result = rai::read (stream_a, type_a);
						if (!result)
						{
							uint16_t extensions_l;
							result = rai::read (stream_a, extensions_l);
							if (!result)
							{
								extensions_a = extensions_l;
							}
						}
                    }
                }
            }
        }
    }
    return result;
}