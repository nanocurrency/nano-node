#include <rai/core/core.hpp>

#include <ed25519-donna/ed25519.h>

#include <unordered_set>
#include <memory>
#include <sstream>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

namespace
{
    bool constexpr ledger_logging ()
    {
        return false;
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
        return network_logging () && false;
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
        return network_logging () && true;
    }
    bool constexpr client_lifetime_tracing ()
    {
        return false;
    }
    bool constexpr insufficient_work_logging ()
    {
        return network_logging () && true;
    }
    bool constexpr log_to_cerr ()
    {
        return false;
    }
}

std::chrono::seconds constexpr rai::processor::period;
std::chrono::seconds constexpr rai::processor::cutoff;

void hash_number (CryptoPP::SHA3 & hash_a, boost::multiprecision::uint256_t const & number_a)
{
    rai::uint256_union bytes (number_a);
    hash_a.Update (bytes.bytes.data (), sizeof (bytes));
}

rai::genesis::genesis ()
{
    send1.hashables.destination.clear ();
    send1.hashables.balance = std::numeric_limits <rai::uint128_t>::max ();
    send1.hashables.previous.clear ();
    send1.signature.clear ();
    send2.hashables.destination = genesis_address;
    send2.hashables.balance.clear ();
    send2.hashables.previous = send1.hash ();
    send2.signature.clear ();
    open.hashables.source = send2.hash ();
    open.hashables.representative = genesis_address;
    open.signature.clear ();
}

void rai::genesis::initialize (rai::block_store & store_a) const
{
    assert (store_a.latest_begin () == store_a.latest_end ());
    store_a.block_put (send1.hash (), send1);
    store_a.block_put (send2.hash (), send2);
    store_a.block_put (open.hash (), open);
    store_a.latest_put (send2.hashables.destination, {open.hash (), open.hashables.representative, send1.hashables.balance, store_a.now ()});
    store_a.representation_put (send2.hashables.destination, send1.hashables.balance.number ());
    store_a.checksum_put (0, 0, hash ());
}

rai::network::network (boost::asio::io_service & service_a, uint16_t port, rai::client & client_a) :
socket (service_a, boost::asio::ip::udp::endpoint (boost::asio::ip::address_v4::any (), port)),
service (service_a),
resolver (service_a),
client (client_a),
keepalive_req_count (0),
keepalive_ack_count (0),
publish_req_count (0),
confirm_req_count (0),
confirm_ack_count (0),
confirm_unk_count (0),
bad_sender_count (0),
unknown_count (0),
error_count (0),
insufficient_work_count (0),
on (true)
{
}

void rai::network::receive ()
{
    std::unique_lock <std::mutex> lock (mutex);
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

void rai::network::send_keepalive (boost::asio::ip::udp::endpoint const & endpoint_a)
{
    rai::keepalive_req message;
    client.peers.random_fill (message.peers);
    std::shared_ptr <std::vector <uint8_t>> bytes (new std::vector <uint8_t>);
    {
        rai::vectorstream stream (*bytes);
        message.serialize (stream);
    }
    if (network_keepalive_logging ())
    {
        client.log.add (boost::str (boost::format ("Keepalive req sent from %1% to %2%") % endpoint ()% endpoint_a));
    }
    auto client_l (client.shared ());
    send_buffer (bytes->data (), bytes->size (), endpoint_a, [bytes, client_l, endpoint_a] (boost::system::error_code const & ec, size_t)
        {
            if (network_logging ())
            {
                if (ec)
                {
                    client_l->log.add (boost::str (boost::format ("Error sending keepalive from %1% to %2% %3%") % client_l->network.endpoint () % endpoint_a % ec.message ()));
                }
            }
        });
}

void rai::network::publish_block (boost::asio::ip::udp::endpoint const & endpoint_a, std::unique_ptr <rai::block> block)
{
    if (network_publish_logging ())
    {
        client.log.add (boost::str (boost::format ("Publish %1% to %2%") % block->hash ().to_string () % endpoint_a));
    }
    rai::publish message (std::move (block));
    rai::work work;
    message.work = work.create (message.block->hash ());
    std::shared_ptr <std::vector <uint8_t>> bytes (new std::vector <uint8_t>);
    {
        rai::vectorstream stream (*bytes);
        message.serialize (stream);
    }
    auto & client_l (client);
    send_buffer (bytes->data (), bytes->size (), endpoint_a, [bytes, &client_l] (boost::system::error_code const & ec, size_t size)
        {
            if (network_logging ())
            {
                if (ec)
                {
                    client_l.log.add (boost::str (boost::format ("Error sending publish: %1%") % ec.message ()));
                }
            }
        });
}

void rai::network::send_confirm_req (boost::asio::ip::udp::endpoint const & endpoint_a, rai::block const & block)
{
    rai::confirm_req message;
	message.block = block.clone ();
    rai::work work;
    message.work = work.create (message.block->hash ());
    std::shared_ptr <std::vector <uint8_t>> bytes (new std::vector <uint8_t>);
    {
        rai::vectorstream stream (*bytes);
        message.serialize (stream);
    }
    if (network_logging ())
    {
        client.log.add (boost::str (boost::format ("Sending confirm req to %1%") % endpoint_a));
    }
    auto & client_l (client);
    send_buffer (bytes->data (), bytes->size (), endpoint_a, [bytes, &client_l] (boost::system::error_code const & ec, size_t size)
        {
            if (network_logging ())
            {
                if (ec)
                {
                    client_l.log.add (boost::str (boost::format ("Error sending confirm request: %1%") % ec.message ()));
                }
            }
        });
}

void rai::network::receive_action (boost::system::error_code const & error, size_t size_a)
{
    if (!error && on)
    {
        if (!rai::reserved_address (remote) && remote != endpoint ())
        {
            if (size_a >= sizeof (rai::message_type))
            {
                auto sender (remote);
                auto known_peer (client.peers.known_peer (sender));
                if (!known_peer)
                {
                    send_keepalive (sender);
                }
                rai::bufferstream type_stream (buffer.data (), size_a);
                rai::message_type type;
                read (type_stream, type);
                switch (type)
                {
                    case rai::message_type::keepalive_req:
                    {
                        rai::keepalive_req incoming;
                        rai::bufferstream stream (buffer.data (), size_a);
                        auto error (incoming.deserialize (stream));
                        receive ();
                        if (!error)
                        {
							++keepalive_req_count;
							client.processor.process_message (incoming, sender, known_peer);
                        }
						else
						{
							++error_count;
						}
                        break;
                    }
                    case rai::message_type::keepalive_ack:
                    {
                        rai::keepalive_ack incoming;
                        rai::bufferstream stream (buffer.data (), size_a);
                        auto error (incoming.deserialize (stream));
                        receive ();
                        if (!error)
                        {
                            ++keepalive_ack_count;
							client.processor.process_message (incoming, sender, known_peer);
                        }
						else
						{
							++error_count;
						}
                        break;
                    }
                    case rai::message_type::publish:
                    {
                        rai::publish incoming;
                        rai::bufferstream stream (buffer.data (), size_a);
                        auto error (incoming.deserialize (stream));
                        receive ();
                        if (!error)
                        {
                            if (!work.validate (incoming.block->hash (), incoming.work))
                            {
                                ++publish_req_count;
                                client.processor.process_message (incoming, sender, known_peer);
                            }
                            else
                            {
                                ++insufficient_work_count;
                                if (insufficient_work_logging ())
                                {
                                    client.log.add ("Insufficient work for publish");
                                }
                            }
                        }
                        else
                        {
                            ++error_count;
                        }
                        break;
                    }
                    case rai::message_type::confirm_req:
                    {
                        rai::confirm_req incoming;
                        rai::bufferstream stream (buffer.data (), size_a);
                        auto error (incoming.deserialize (stream));
                        receive ();
                        if (!error)
                        {
                            if (!work.validate (incoming.block->hash (), incoming.work))
                            {
                                ++confirm_req_count;
                                client.processor.process_message (incoming, sender, known_peer);
                            }
                            else
                            {
                                ++insufficient_work_count;
                                if (insufficient_work_logging ())
                                {
                                    client.log.add ("Insufficient work for confirm_req");
                                }
                            }
                        }
						else
						{
							++error_count;
						}
                        break;
                    }
                    case rai::message_type::confirm_ack:
                    {
                        rai::confirm_ack incoming;
                        rai::bufferstream stream (buffer.data (), size_a);
                        auto error (incoming.deserialize (stream));
                        receive ();
                        if (!error)
                        {
							++confirm_ack_count;
							client.processor.process_message (incoming, sender, known_peer);
                        }
						else
						{
							++error_count;
						}
                        break;
                    }
                    case rai::message_type::confirm_unk:
                    {
                        ++confirm_unk_count;
                        auto incoming (new rai::confirm_unk);
                        rai::bufferstream stream (buffer.data (), size_a);
                        auto error (incoming->deserialize (stream));
                        receive ();
                        break;
                    }
                    default:
                    {
                        ++unknown_count;
                        receive ();
                        break;
                    }
                }
            }
        }
        else
        {
            ++bad_sender_count;
            if (network_logging ())
            {
                client.log.add ("Reserved sender");
            }
        }
    }
    else
    {
        if (network_logging ())
        {
            client.log.add ("Receive error");
        }
    }
}

void rai::network::merge_peers (std::array <rai::endpoint, 24> const & peers_a)
{
    rai::keepalive_req req_message;
    client.peers.random_fill (req_message.peers);
    std::shared_ptr <std::vector <uint8_t>> req_bytes (new std::vector <uint8_t>);
    {
        rai::vectorstream stream (*req_bytes);
        req_message.serialize (stream);
    }
    for (auto i (peers_a.begin ()), j (peers_a.end ()); i != j; ++i) // Amplify attack, send to the same IP many times
    {
        if (!client.peers.contacting_peer (*i) && *i != endpoint ())
        {
            if (network_keepalive_logging ())
            {
                client.log.add (boost::str (boost::format ("Sending keepalive req to %1%") % i));
            }
            auto & client_l (client);
            auto endpoint (*i);
            send_buffer (req_bytes->data (), req_bytes->size (), endpoint, [req_bytes, &client_l] (boost::system::error_code const & error, size_t size_a)
                {
                    if (network_logging ())
                    {
                        if (error)
                        {
                            client_l.log.add (boost::str (boost::format ("Error sending keepalive request: %1%") % error.message ()));
                        }
                    }
                });
        }
        else
        {
            if (network_logging ())
            {
                if (rai::reserved_address (*i))
                {
                    if (i->address ().to_v4 ().to_ulong () != 0 || i->port () != 0)
                    {
                        client.log.add (boost::str (boost::format ("Keepalive req contained reserved address")));
                    }
                }
            }
        }
    }
}

rai::publish::publish (std::unique_ptr <rai::block> block_a) :
block (std::move (block_a))
{
}

bool rai::publish::deserialize (rai::stream & stream_a)
{
    rai::message_type type;
    auto result (read (stream_a, type));
    assert (!result);
    if (!result)
    {
        result = read (stream_a, work);
        if (!result)
        {
            block = rai::deserialize_block (stream_a);
            result = block == nullptr;
        }
    }
    return result;
}

void rai::publish::serialize (rai::stream & stream_a)
{
    write (stream_a, rai::message_type::publish);
    write (stream_a, work);
    rai::serialize_block (stream_a, *block);
}

namespace
{
class xorshift1024star
{
public:
    xorshift1024star ():
    p (0)
    {
    }
    std::array <uint64_t, 16> s;
    unsigned p;
    uint64_t next ()
    {
        auto p_l (p);
        auto pn ((p_l + 1) & 15);
        p = pn;
        uint64_t s0 = s[ p_l ];
        uint64_t s1 = s[ pn ];
        s1 ^= s1 << 31; // a
        s1 ^= s1 >> 11; // b
        s0 ^= s0 >> 30; // c
        return ( s[ pn ] = s0 ^ s1 ) * 1181783497276652981LL;
    }
};
}

rai::uint256_union const rai::wallet::version_1 (1);
rai::uint256_union const rai::wallet::version_current (version_1);
rai::uint256_union const rai::wallet::version_special (0);
rai::uint256_union const rai::wallet::salt_special (1);
rai::uint256_union const rai::wallet::wallet_key_special (2);
rai::uint256_union const rai::wallet::check_special (3);
int const rai::wallet::special_count (4);

rai::wallet::wallet (bool & init_a, boost::filesystem::path const & path_a) :
password (0, 1024)
{
    boost::system::error_code code;
    boost::filesystem::create_directories (path_a, code);
    if (!code)
    {
        leveldb::Options options;
        options.create_if_missing = true;
        leveldb::DB * db (nullptr);
        auto status (leveldb::DB::Open (options, (path_a / "wallet.ldb").string (), &db));
        handle.reset (db);
        if (status.ok ())
        {
            rai::uint256_union wallet_password_key;
            wallet_password_key.clear ();
            std::string wallet_password_value;
            auto wallet_password_status (handle->Get (leveldb::ReadOptions (), leveldb::Slice (wallet_password_key.chars.data (), wallet_password_key.chars.size ()), &wallet_password_value));
            if (wallet_password_status.IsNotFound ())
            {
                // The wallet is empty meaning we just created it, initialize it.
                auto version_status (handle->Put (leveldb::WriteOptions (), leveldb::Slice (version_special.chars.data (), version_special.chars.size ()), leveldb::Slice (version_current.chars.data (), version_current.chars.size ())));
                assert (version_status.ok ());
                // Wallet key is a fixed random key that encrypts all entries
                rai::uint256_union salt_l;
                random_pool.GenerateBlock (salt_l.bytes.data (), salt_l.bytes.size ());
                auto status3 (handle->Put (leveldb::WriteOptions (), leveldb::Slice (salt_special.chars.data (), salt_special.chars.size ()), leveldb::Slice (salt_l.chars.data (), salt_l.chars.size ())));
                assert (status3.ok ());
                auto password_l (derive_key (""));
                password.value_set (password_l);
                rai::uint256_union wallet_key;
                random_pool.GenerateBlock (wallet_key.bytes.data (), sizeof (wallet_key.bytes));
                // Wallet key is encrypted by the user's password
                rai::uint256_union encrypted (wallet_key, password_l, salt_l.owords [0]);
                // Wallet key is stored in entry 0
                auto status1 (handle->Put (leveldb::WriteOptions (), leveldb::Slice (wallet_key_special.chars.data (), wallet_key_special.chars.size ()), leveldb::Slice (encrypted.chars.data (), encrypted.chars.size ())));
                assert (status1.ok ());
                rai::uint256_union zero (0);
                rai::uint256_union check (zero, wallet_key, salt_l.owords [0]);
                // Check key is stored in entry 1
                auto status2 (handle->Put (leveldb::WriteOptions (), leveldb::Slice (check_special.chars.data (), check_special.chars.size ()), leveldb::Slice (check.chars.data (), check.chars.size ())));
                assert (status2.ok ());
                wallet_key.clear ();
                password_l.clear ();
            }
            else
            {
                auto password_l (derive_key (""));
                password.value_set (password_l);
            }
            init_a = false;
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

void rai::wallet::insert (rai::private_key const & prv)
{
    rai::public_key pub;
    ed25519_publickey (prv.bytes.data (), pub.bytes.data ());
    rai::uint256_union encrypted (prv, wallet_key (), salt ().owords [0]);
    auto status (handle->Put (leveldb::WriteOptions (), leveldb::Slice (pub.chars.data (), pub.chars.size ()), leveldb::Slice (encrypted.chars.data (), encrypted.chars.size ())));
    assert (status.ok ());
}

bool rai::wallet::fetch (rai::public_key const & pub, rai::private_key & prv)
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

rai::key_iterator rai::wallet::begin ()
{
    rai::key_iterator result (handle.get ());
    for (auto i (0); i < special_count; ++i)
    {
        assert (result != end ());
        ++result;
    }
    return result;
}

rai::key_iterator rai::wallet::find (rai::uint256_union const & key)
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

rai::key_iterator rai::wallet::end ()
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

bool rai::wallet::generate_send (rai::ledger & ledger_a, rai::public_key const & destination, rai::uint128_t const & amount_a, std::vector <std::unique_ptr <rai::send_block>> & blocks)
{
    bool result (false);
    rai::uint128_t remaining (amount_a);
    for (auto i (begin ()), j (end ()); i != j && !result && !remaining.is_zero (); ++i)
    {
        auto account (i->first);
        auto balance (ledger_a.account_balance (account));
        if (!balance.is_zero ())
        {
            rai::frontier frontier;
            result = ledger_a.store.latest_get (account, frontier);
            assert (!result);
            auto amount (std::min (remaining, balance));
            remaining -= amount;
            std::unique_ptr <rai::send_block> block (new rai::send_block);
            block->hashables.destination = destination;
            block->hashables.previous = frontier.hash;
            block->hashables.balance = balance - amount;
            rai::private_key prv;
            result = fetch (account, prv);
            assert (!result);
            sign_message (prv, account, block->hash (), block->signature);
            prv.clear ();
            blocks.push_back (std::move (block));
        }
    }
    if (!remaining.is_zero ())
    {
        result = true;
        blocks.clear ();
    }
    return result;
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

rai::client::client (rai::client_init & init_a, boost::shared_ptr <boost::asio::io_service> service_a, uint16_t port_a, boost::filesystem::path const & data_path_a, rai::processor_service & processor_a, rai::address const & representative_a) :
representative (representative_a),
store (init_a.block_store_init, data_path_a),
ledger (init_a.ledger_init, init_a.block_store_init, store),
conflicts (*this),
wallet (init_a.wallet_init, data_path_a),
network (*service_a, port_a, *this),
bootstrap (*service_a, port_a, *this),
processor (*this),
transactions (*this),
peers (network.endpoint ()),
service (processor_a)
{
    if (!init_a.error ())
    {
        if (client_lifetime_tracing ())
        {
            std::cerr << "Constructing client\n";
        }
        if (store.latest_begin () == store.latest_end ())
        {
            rai::genesis genesis;
            genesis.initialize (store);
        }
    }
}

rai::client::client (rai::client_init & init_a, boost::shared_ptr <boost::asio::io_service> service_a, uint16_t port_a, rai::processor_service & processor_a, rai::address const & representative_a) :
client (init_a, service_a, port_a, boost::filesystem::unique_path (), processor_a, representative_a)
{
}

rai::client::~client ()
{
    if (client_lifetime_tracing ())
    {
        std::cerr << "Destructing client\n";
    }
}

namespace
{
class publish_processor : public std::enable_shared_from_this <publish_processor>
{
public:
    publish_processor (std::shared_ptr <rai::client> client_a, std::unique_ptr <rai::block> incoming_a, rai::endpoint const & sender_a) :
    client (client_a),
    incoming (std::move (incoming_a)),
    sender (sender_a),
    attempts (0)
    {
    }
    void run ()
    {
        auto hash (incoming->hash ());
        auto list (client->peers.list ());
        if (network_publish_logging ())
        {
            client->log.add (boost::str (boost::format ("Publishing %1% to %2% peers") % hash.to_string () % list.size ()));
        }
        for (auto i (list.begin ()), j (list.end ()); i != j; ++i)
        {
            if (i->endpoint != sender)
            {
                client->network.publish_block (i->endpoint, incoming->clone ());
            }
        }
        if (attempts < 0)
        {
            ++attempts;
            auto this_l (shared_from_this ());
            client->service.add (std::chrono::system_clock::now () + std::chrono::seconds (15), [this_l] () {this_l->run ();});
            if (network_publish_logging ())
            {
                client->log.add (boost::str (boost::format ("Queueing another publish for %1%") % hash.to_string ()));
            }
        }
        else
        {
            if (network_publish_logging ())
            {
                client->log.add (boost::str (boost::format ("Done publishing for %1%") % hash.to_string ()));
            }
        }
    }
    std::shared_ptr <rai::client> client;
    std::unique_ptr <rai::block> incoming;
    rai::endpoint sender;
    int attempts;
};
}

void rai::processor::republish (std::unique_ptr <rai::block> incoming_a, rai::endpoint const & sender_a)
{
    auto republisher (std::make_shared <publish_processor> (client.shared (), incoming_a->clone (), sender_a));
    republisher->run ();
}

namespace {
class republish_visitor : public rai::block_visitor
{
public:
    republish_visitor (std::shared_ptr <rai::client> client_a, std::unique_ptr <rai::block> incoming_a, rai::endpoint const & sender_a) :
    client (client_a),
    incoming (std::move (incoming_a)),
    sender (sender_a)
    {
        assert (client_a->store.block_exists (incoming->hash ()));
    }
    void send_block (rai::send_block const & block_a)
    {
        if (client->wallet.find (block_a.hashables.destination) == client->wallet.end ())
        {
            client->processor.republish (std::move (incoming), sender);
        }
    }
    void receive_block (rai::receive_block const & block_a)
    {
        client->processor.republish (std::move (incoming), sender);
    }
    void open_block (rai::open_block const & block_a)
    {
        client->processor.republish (std::move (incoming), sender);
    }
    void change_block (rai::change_block const & block_a)
    {
        client->processor.republish (std::move (incoming), sender);
    }
    std::shared_ptr <rai::client> client;
    std::unique_ptr <rai::block> incoming;
    rai::endpoint sender;
};
}

rai::gap_cache::gap_cache () :
max (128)
{
}

void rai::gap_cache::add (rai::block const & block_a, rai::block_hash needed_a)
{
    auto existing (blocks.find (needed_a));
    if (existing != blocks.end ())
    {
        blocks.modify (existing, [] (rai::gap_information & info) {info.arrival = std::chrono::system_clock::now ();});
    }
    else
    {
        blocks.insert ({std::chrono::system_clock::now (), needed_a, block_a.clone ()});
        if (blocks.size () > max)
        {
            blocks.get <1> ().erase (blocks.get <1> ().begin ());
        }
    }
}

std::unique_ptr <rai::block> rai::gap_cache::get (rai::block_hash const & hash_a)
{
    std::unique_ptr <rai::block> result;
    auto existing (blocks.find (hash_a));
    if (existing != blocks.end ())
    {
        blocks.modify (existing, [&] (rai::gap_information & info) {result.swap (info.block);});
        blocks.erase (existing);
    }
    return result;
}

void rai::election::vote (rai::vote const & vote_a)
{
    votes.vote (vote_a);
    if (!confirmed)
    {
        auto winner_l (votes.winner ());
        if (votes.rep_votes.size () == 1)
        {
            if (winner_l.second > uncontested_threshold ())
            {
                confirmed = true;
                client->processor.process_confirmed (*votes.last_winner);
            }
        }
        else
        {
            if (winner_l.second > contested_threshold ())
            {
                confirmed = true;
                client->processor.process_confirmed (*votes.last_winner);
            }
        }
    }
}

void rai::election::start_request (rai::block const & block_a)
{
    auto list (client->peers.list ());
    for (auto i (list.begin ()), j (list.end ()); i != j; ++i)
    {
        client->network.send_confirm_req (i->endpoint, block_a);
    }
}

void rai::election::announce_vote ()
{
    auto winner_l (votes.winner ());
	assert (winner_l.first != nullptr);
    client->network.confirm_block (std::move (winner_l.first), votes.sequence);
    auto now (std::chrono::system_clock::now ());
    if (now - last_vote < std::chrono::seconds (15))
    {
        auto this_l (shared_from_this ());
        client->service.add (now + std::chrono::seconds (15), [this_l] () {this_l->announce_vote ();});
    }
}

void rai::network::confirm_block (std::unique_ptr <rai::block> block_a, uint64_t sequence_a)
{
    rai::confirm_ack confirm;
    confirm.vote.address = client.representative;
    confirm.vote.sequence = sequence_a;
    confirm.vote.block = std::move (block_a);
    rai::private_key prv;
    auto error (client.wallet.fetch (client.representative, prv));
    assert (!error);
    rai::sign_message (prv, client.representative, confirm.vote.hash (), confirm.vote.signature);
    prv.clear ();
    std::shared_ptr <std::vector <uint8_t>> bytes (new std::vector <uint8_t>);
    {
        rai::vectorstream stream (*bytes);
        confirm.serialize (stream);
    }
    auto & client_l (client);
    auto list (client.peers.list ());
    for (auto i (list.begin ()), n (list.end ()); i != n; ++i)
    {
        client.network.send_buffer (bytes->data (), bytes->size (), i->endpoint, [bytes, &client_l] (boost::system::error_code const & ec, size_t size_a)
            {
                if (network_logging ())
                {
                    if (ec)
                    {
                        client_l.log.add (boost::str (boost::format ("Error broadcasting confirmation: %1%") % ec.message ()));
                    }
                }
            });
    }
}

void rai::processor::process_receive_republish (std::unique_ptr <rai::block> incoming, rai::endpoint const & sender_a)
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
                republish_visitor visitor (client.shared (), std::move (block), sender_a);
                visitor.incoming->visit (visitor);
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

namespace
{
class receivable_visitor : public rai::block_visitor
{
public:
    receivable_visitor (rai::client & client_a, rai::block const & incoming_a) :
    client (client_a),
    incoming (incoming_a)
    {
    }
    void send_block (rai::send_block const & block_a) override
    {
        if (client.wallet.find (block_a.hashables.destination) != client.wallet.end ())
        {
            auto root (incoming.previous ());
            assert (!root.is_zero ());
            client.conflicts.start (block_a, true);
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
    rai::block const & incoming;
};
    
class progress_log_visitor : public rai::block_visitor
{
public:
    progress_log_visitor (rai::client & client_a) :
    client (client_a)
    {
    }
    void send_block (rai::send_block const & block_a) override
    {
        client.log.add (boost::str (boost::format ("Sending from:\n\t%1% to:\n\t%2% amount:\n\t%3% previous:\n\t%4% block:\n\t%5%") % client.ledger.account (block_a.hash ()).to_string () % block_a.hashables.destination.to_string () % client.ledger.amount (block_a.hash ()) % block_a.hashables.previous.to_string () % block_a.hash ().to_string ()));
    }
    void receive_block (rai::receive_block const & block_a) override
    {
        client.log.add (boost::str (boost::format ("Receiving from:\n\t%1% to:\n\t%2% previous:\n\t%3% block:\n\t%4%") % client.ledger.account (block_a.hashables.source).to_string () % client.ledger.account (block_a.hash ()).to_string () %block_a.hashables.previous.to_string () % block_a.hash ().to_string ()));
    }
    void open_block (rai::open_block const & block_a) override
    {
        client.log.add (boost::str (boost::format ("Open from:\n\t%1% to:\n\t%2% block:\n\t%3%") % client.ledger.account (block_a.hashables.source).to_string () % client.ledger.account (block_a.hash ()).to_string () % block_a.hash ().to_string ()));
    }
    void change_block (rai::change_block const & block_a) override
    {
    }
    rai::client & client;
};
	
class successor_visitor : public rai::block_visitor
{
public:
    void send_block (rai::send_block const & block_a) override
    {
    }
    void receive_block (rai::receive_block const & block_a) override
    {
    }
    void open_block (rai::open_block const & block_a) override
    {
    }
    void change_block (rai::change_block const & block_a) override
    {
    }
};
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
                progress_log_visitor logger (client);
                block_a.visit (logger);
            }
            receivable_visitor visitor (client, block_a);
            block_a.visit (visitor);
            break;
        }
        case rai::process_result::gap_previous:
        {
            if (ledger_logging ())
            {
                client.log.add (boost::str (boost::format ("Gap previous for: %1%") % block_a.hash ().to_string ()));
            }
            auto previous (block_a.previous ());
            client.gap_cache.add (block_a, previous);
            break;
        }
        case rai::process_result::gap_source:
        {
            if (ledger_logging ())
            {
                client.log.add (boost::str (boost::format ("Gap source for: %1%") % block_a.hash ().to_string ()));
            }
            auto source (block_a.source ());
            client.gap_cache.add (block_a, source);
            break;
        }
        case rai::process_result::old:
        {
            if (ledger_duplicate_logging ())
            {
                client.log.add (boost::str (boost::format ("Old for: %1%") % block_a.hash ().to_string ()));
            }
            break;
        }
        case rai::process_result::bad_signature:
        {
            if (ledger_logging ())
            {
                client.log.add (boost::str (boost::format ("Bad signature for: %1%") % block_a.hash ().to_string ()));
            }
            break;
        }
        case rai::process_result::overspend:
        {
            if (ledger_logging ())
            {
                client.log.add (boost::str (boost::format ("Overspend for: %1%") % block_a.hash ().to_string ()));
            }
            break;
        }
        case rai::process_result::overreceive:
        {
            if (ledger_logging ())
            {
                client.log.add (boost::str (boost::format ("Overreceive for: %1%") % block_a.hash ().to_string ()));
            }
            break;
        }
        case rai::process_result::not_receive_from_send:
        {
            if (ledger_logging ())
            {
                client.log.add (boost::str (boost::format ("Not receive from spend for: %1%") % block_a.hash ().to_string ()));
            }
            break;
        }
        case rai::process_result::fork_source:
        {
            if (ledger_logging ())
            {
                client.log.add (boost::str (boost::format ("Fork source for: %1%") % block_a.hash ().to_string ()));
            }
            client.conflicts.start (*client.ledger.successor (client.store.root (block_a)), false);
            break;
        }
        case rai::process_result::fork_previous:
        {
            if (ledger_logging ())
            {
                client.log.add (boost::str (boost::format ("Fork previous for: %1%") % block_a.hash ().to_string ()));
            }
            client.conflicts.start (*client.ledger.successor (client.store.root (block_a)), false);
            break;
        }
    }
    return result;
}

void rai::peer_container::incoming_from_peer (rai::endpoint const & endpoint_a)
{
	assert (!reserved_address (endpoint_a));
	if (endpoint_a != self)
	{
		std::lock_guard <std::mutex> lock (mutex);
		auto existing (peers.find (endpoint_a));
		if (existing == peers.end ())
		{
			peers.insert ({endpoint_a, std::chrono::system_clock::now (), std::chrono::system_clock::now ()});
		}
		else
		{
			peers.modify (existing, [] (rai::peer_information & info) {info.last_contact = std::chrono::system_clock::now (); info.last_attempt = std::chrono::system_clock::now ();});
		}
	}
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

void rai::keepalive_req::visit (rai::message_visitor & visitor_a) const
{
    visitor_a.keepalive_req (*this);
}

void rai::keepalive_ack::visit (rai::message_visitor & visitor_a) const
{
    visitor_a.keepalive_ack (*this);
}

void rai::publish::visit (rai::message_visitor & visitor_a) const
{
    visitor_a.publish (*this);
}

void rai::keepalive_ack::serialize (rai::stream & stream_a)
{
    write (stream_a, rai::message_type::keepalive_ack);
    for (auto i (peers.begin ()), j (peers.end ()); i != j; ++i)
    {
        uint32_t address (i->address ().to_v4 ().to_ulong ());
        write (stream_a, address);
        write (stream_a, i->port ());
    }
	write (stream_a, checksum);
}

bool rai::keepalive_ack::deserialize (rai::stream & stream_a)
{
    rai::message_type type;
    auto result (read (stream_a, type));
    assert (type == rai::message_type::keepalive_ack);
    for (auto i (peers.begin ()), j (peers.end ()); i != j; ++i)
    {
        uint32_t address;
        uint16_t port;
        read (stream_a, address);
        read (stream_a, port);
        *i = rai::endpoint (boost::asio::ip::address_v4 (address), port);
    }
	read (stream_a, checksum);
    return result;
}

void rai::keepalive_req::serialize (rai::stream & stream_a)
{
    write (stream_a, rai::message_type::keepalive_req);
    for (auto i (peers.begin ()), j (peers.end ()); i != j; ++i)
    {
        uint32_t address (i->address ().to_v4 ().to_ulong ());
        write (stream_a, address);
        write (stream_a, i->port ());
    }
}

bool rai::keepalive_req::deserialize (rai::stream & stream_a)
{
    rai::message_type type;
    auto result (read (stream_a, type));
    assert (type == rai::message_type::keepalive_req);
    for (auto i (peers.begin ()), j (peers.end ()); i != j; ++i)
    {
        uint32_t address;
        uint16_t port;
        read (stream_a, address);
        read (stream_a, port);
        *i = rai::endpoint (boost::asio::ip::address_v4 (address), port);
    }
    return result;
}

size_t rai::processor_service::size ()
{
    std::lock_guard <std::mutex> lock (mutex);
    return operations.size ();
}

bool rai::client::send (rai::public_key const & address, rai::uint128_t const & amount_a)
{
    return transactions.send (address, amount_a);
}

rai::system::system (uint16_t port_a, size_t count_a) :
service (new boost::asio::io_service)
{
    clients.reserve (count_a);
    for (size_t i (0); i < count_a; ++i)
    {
        rai::client_init init;
        auto client (std::make_shared <rai::client> (init, service, port_a + i, processor, rai::genesis_address));
        assert (!init.error ());
        client->start ();
        clients.push_back (client);
    }
    for (auto i (clients.begin ()), j (clients.begin () + 1), n (clients.end ()); j != n; ++i, ++j)
    {
        auto starting1 ((*i)->peers.size ());
        auto starting2 ((*j)->peers.size ());
        (*j)->network.send_keepalive ((*i)->network.endpoint ());
        do {
            service->run_one ();
        } while ((*i)->peers.size () == starting1 || (*j)->peers.size () == starting2);
    }
}

rai::system::~system ()
{
    for (auto & i: clients)
    {
        i->stop ();
    }
}

void rai::processor::process_unknown (rai::vectorstream & stream_a)
{
	rai::confirm_unk outgoing;
	outgoing.rep_hint = client.representative;
	outgoing.serialize (stream_a);
}

void rai::processor::process_confirmation (rai::block const & block_a, rai::endpoint const & sender)
{
    std::shared_ptr <std::vector <uint8_t>> bytes (new std::vector <uint8_t>);
	{
		rai::vectorstream stream (*bytes);
		if (!client.is_representative ())
		{
			process_unknown (stream);
		}
		else
		{
			auto weight (client.ledger.weight (client.representative));
			if (weight.is_zero ())
			{
				process_unknown (stream);
			}
			else
			{
                rai::private_key prv;
                auto error (client.wallet.fetch (client.representative, prv));
                assert (!error);
				rai::confirm_ack outgoing;
				outgoing.vote.address = client.representative;
                outgoing.vote.block = block_a.clone ();
				outgoing.vote.sequence = 0;
				rai::sign_message (prv, client.representative, outgoing.vote.hash (), outgoing.vote.signature);
				assert (!rai::validate_message (client.representative, outgoing.vote.hash (), outgoing.vote.signature));
                outgoing.serialize (stream);
			}
		}
	}
    auto & client_l (client);
    if (network_message_logging ())
    {
        client_l.log.add (boost::str (boost::format ("Sending confirm ack to: %1%") % sender));
    }
    client.network.send_buffer (bytes->data (), bytes->size (), sender, [bytes, &client_l] (boost::system::error_code const & ec, size_t size_a)
        {
            if (network_logging ())
            {
                if (ec)
                {
                    client_l.log.add (boost::str (boost::format ("Error sending confirm ack: %1%") % ec.message ()));
                }
            }
        });
}

rai::key_entry * rai::key_entry::operator -> ()
{
    return this;
}

bool rai::confirm_ack::deserialize (rai::stream & stream_a)
{
    rai::message_type type;
    auto result (read (stream_a, type));
    assert (type == rai::message_type::confirm_ack);
    if (!result)
    {
        result = read (stream_a, vote.address);
        if (!result)
        {
            result = read (stream_a, vote.signature);
            if (!result)
            {
                result = read (stream_a, vote.sequence);
                if (!result)
                {
                    vote.block = rai::deserialize_block (stream_a);
                    result = vote.block == nullptr;
                }
            }
        }
    }
    return result;
}

void rai::confirm_ack::serialize (rai::stream & stream_a)
{
    write (stream_a, rai::message_type::confirm_ack);
    write (stream_a, vote.address);
    write (stream_a, vote.signature);
    write (stream_a, vote.sequence);
    rai::serialize_block (stream_a, *vote.block);
}

bool rai::confirm_ack::operator == (rai::confirm_ack const & other_a) const
{
    auto result (vote.address == other_a.vote.address && *vote.block == *other_a.vote.block && vote.signature == other_a.vote.signature && vote.sequence == other_a.vote.sequence);
    return result;
}

void rai::confirm_ack::visit (rai::message_visitor & visitor_a) const
{
    visitor_a.confirm_ack (*this);
}

bool rai::confirm_req::deserialize (rai::stream & stream_a)
{
    rai::message_type type;
    read (stream_a, type);
    assert (type == rai::message_type::confirm_req);
    auto result (read (stream_a, work));
    if (!result)
    {
        block = rai::deserialize_block (stream_a);
        result = block == nullptr;
    }
    return result;
}

bool rai::confirm_unk::deserialize (rai::stream & stream_a)
{
    rai::message_type type;
    read (stream_a, type);
    assert (type == rai::message_type::confirm_unk);
    auto result (read (stream_a, rep_hint));
    return result;
}

void rai::confirm_req::visit (rai::message_visitor & visitor_a) const
{
    visitor_a.confirm_req (*this);
}

void rai::confirm_unk::visit (rai::message_visitor & visitor_a) const
{
    visitor_a.confirm_unk (*this);
}

void rai::confirm_req::serialize (rai::stream & stream_a)
{
    assert (block != nullptr);
    write (stream_a, rai::message_type::confirm_req);
    write (stream_a, work);
    rai::serialize_block (stream_a, *block);
}

rai::rpc::rpc (boost::shared_ptr <boost::asio::io_service> service_a, boost::shared_ptr <boost::network::utils::thread_pool> pool_a, uint16_t port_a, rai::client & client_a, bool enable_control_a) :
server (decltype (server)::options (*this).address ("0.0.0.0").port (std::to_string (port_a)).io_service (service_a).thread_pool (pool_a)),
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
            if (action == "account_balance")
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
            else if (action == "wallet_create")
            {
                if (enable_control)
                {
                    rai::keypair new_key;
                    client.wallet.insert (new_key.prv);
                    boost::property_tree::ptree response_l;
                    std::string account;
                    new_key.pub.encode_base58check (account);
                    response_l.put ("account", account);
                    set_response (response, response_l);
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
                rai::uint256_union account;
                auto error (account.decode_base58check (account_text));
                if (!error)
                {
                    auto exists (client.wallet.find (account) != client.wallet.end ());
                    boost::property_tree::ptree response_l;
                    response_l.put ("exists", exists ? "1" : "0");
                    set_response (response, response_l);
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "Bad account number";
                }
            }
            else if (action == "wallet_list")
            {
                boost::property_tree::ptree response_l;
                boost::property_tree::ptree accounts;
                for (auto i (client.wallet.begin ()), j (client.wallet.end ()); i != j; ++i)
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
            else if (action == "wallet_add")
            {
                if (enable_control)
                {
                    std::string key_text (request_l.get <std::string> ("key"));
                    rai::private_key key;
                    auto error (key.decode_hex (key_text));
                    if (!error)
                    {
                        client.wallet.insert (key);
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
                    auto valid (client.wallet.valid_password ());
                    boost::property_tree::ptree response_l;
                    response_l.put ("valid", valid ? "1" : "0");
                    set_response (response, response_l);
                }
                else
                {
                    response = boost::network::http::server<rai::rpc>::response::stock_reply (boost::network::http::server<rai::rpc>::response::bad_request);
                    response.content = "RPC control is disabled";
                }
            }
            else if (action == "validate_account")
            {
                std::string account_text (request_l.get <std::string> ("account"));
                rai::uint256_union account;
                auto error (account.decode_base58check (account_text));
                boost::property_tree::ptree response_l;
                response_l.put ("valid", error ? "0" : "1");
                set_response (response, response_l);
            }
            else if (action == "send")
            {
                if (enable_control)
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
                            auto error (client.send (account, amount.number ()));
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
                    response.content = "RPC control is disabled";
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

rai::uint128_t rai::block_store::representation_get (rai::address const & address_a)
{
    std::string value;
    auto status (representation->Get (leveldb::ReadOptions (), leveldb::Slice (address_a.chars.data (), address_a.chars.size ()), &value));
    assert (status.ok () || status.IsNotFound ());
    rai::uint128_t result;
    if (status.ok ())
    {
        rai::uint128_union rep;
        rai::bufferstream stream (reinterpret_cast <uint8_t const *> (value.data ()), value.size ());
        auto error (rai::read (stream, rep));
        assert (!error);
        result = rep.number ();
    }
    else
    {
        result = 0;
    }
    return result;
}

void rai::block_store::representation_put (rai::address const & address_a, rai::uint128_t const & representation_a)
{
    rai::uint128_union rep (representation_a);
    auto status (representation->Put (leveldb::WriteOptions (), leveldb::Slice (address_a.chars.data (), address_a.chars.size ()), leveldb::Slice (rep.chars.data (), rep.chars.size ())));
    assert (status.ok ());
}

void rai::confirm_unk::serialize (rai::stream & stream_a)
{
    write (stream_a, rep_hint);
}

std::unique_ptr <rai::block> rai::block_store::fork_get (rai::block_hash const & hash_a)
{
    std::string value;
    auto status (forks->Get (leveldb::ReadOptions (), leveldb::Slice (hash_a.chars.data (), hash_a.chars.size ()), &value));
    assert (status.ok () || status.IsNotFound ());
    std::unique_ptr <rai::block> result;
    if (status.ok ())
    {
        rai::bufferstream stream (reinterpret_cast <uint8_t const *> (value.data ()), value.size ());
        result = rai::deserialize_block (stream);
        assert (result != nullptr);
    }
    return result;
}

void rai::block_store::fork_put (rai::block_hash const & hash_a, rai::block const & block_a)
{
    std::vector <uint8_t> vector;
    {
        rai::vectorstream stream (vector);
        rai::serialize_block (stream, block_a);
    }
    auto status (forks->Put (leveldb::WriteOptions (), leveldb::Slice (hash_a.chars.data (), hash_a.chars.size ()), leveldb::Slice (reinterpret_cast <char const *> (vector.data ()), vector.size ())));
    assert (status.ok ());
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
        rai::address sender;
        rai::amount amount;
        rai::address destination;
		while (ledger.store.pending_get (hash, sender, amount, destination))
		{
			ledger.rollback (ledger.latest (block_a.hashables.destination));
		}
        rai::frontier frontier;
        ledger.store.latest_get (sender, frontier);
		ledger.store.pending_del (hash);
        ledger.change_latest (sender, block_a.hashables.previous, frontier.representative, ledger.balance (block_a.hashables.previous));
		ledger.store.block_del (hash);
    }
    void receive_block (rai::receive_block const & block_a) override
    {
		auto hash (block_a.hash ());
        auto representative (ledger.representative (block_a.hashables.source));
        auto amount (ledger.amount (block_a.hashables.source));
        auto destination_address (ledger.account (hash));
		ledger.move_representation (ledger.representative (hash), representative, amount);
        ledger.change_latest (destination_address, block_a.hashables.previous, representative, ledger.balance (block_a.hashables.previous));
		ledger.store.block_del (hash);
		ledger.store.pending_put (block_a.hashables.source, ledger.account (block_a.hashables.source), amount, destination_address);
    }
    void open_block (rai::open_block const & block_a) override
    {
		auto hash (block_a.hash ());
        auto representative (ledger.representative (block_a.hashables.source));
        auto amount (ledger.amount (block_a.hashables.source));
        auto destination_address (ledger.account (hash));
		ledger.move_representation (ledger.representative (hash), representative, amount);
        ledger.change_latest (destination_address, 0, representative, 0);
		ledger.store.block_del (hash);
		ledger.store.pending_put (block_a.hashables.source, ledger.account (block_a.hashables.source), amount, destination_address);
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

void rai::block_store::block_del (rai::block_hash const & hash_a)
{
    auto status (blocks->Delete (leveldb::WriteOptions (), leveldb::Slice (hash_a.chars.data (), hash_a.chars.size ())));
    assert (status.ok ());
}

void rai::block_store::latest_del (rai::address const & address_a)
{
    auto status (addresses->Delete (leveldb::WriteOptions (), leveldb::Slice (address_a.chars.data (), address_a.chars.size ())));
    assert (status.ok ());
}

bool rai::block_store::latest_exists (rai::address const & address_a)
{
    std::unique_ptr <leveldb::Iterator> existing (addresses->NewIterator (leveldb::ReadOptions {}));
    existing->Seek (leveldb::Slice (address_a.chars.data (), address_a.chars.size ()));
    bool result;
    if (existing->Valid ())
    {
        result = true;
    }
    else
    {
        result = false;
    }
    return result;
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

void rai::bulk_req::visit (rai::message_visitor & visitor_a) const
{
    visitor_a.bulk_req (*this);
}

bool rai::bulk_req::deserialize (rai::stream & stream_a)
{
    rai::message_type type;
    auto result (read (stream_a, type));
    if (!result)
    {
        assert (type == rai::message_type::bulk_req);
        result = read (stream_a, start);
        if (!result)
        {
            result = read (stream_a, end);
        }
    }
    return result;
}

void rai::bulk_req::serialize (rai::stream & stream_a)
{
    write (stream_a, rai::message_type::bulk_req);
    write (stream_a, start);
    write (stream_a, end);
}

void rai::client::start ()
{
    network.receive ();
    processor.ongoing_keepalive ();
    bootstrap.start ();
}

void rai::client::stop ()
{
    network.stop ();
    bootstrap.stop ();
    service.stop ();
}

void rai::processor::bootstrap (boost::asio::ip::tcp::endpoint const & endpoint_a, std::function <void ()> const & complete_action_a)
{
    auto processor (std::make_shared <rai::bootstrap_initiator> (client.shared (), complete_action_a));
    processor->run (endpoint_a);
}

void rai::processor::connect_bootstrap (std::vector <std::string> const & peers_a)
{
    auto client_l (client.shared ());
    client.service.add (std::chrono::system_clock::now (), [client_l, peers_a] ()
    {
        for (auto i (peers_a.begin ()), n (peers_a.end ()); i != n; ++i)
        {
            client_l->network.resolver.async_resolve (boost::asio::ip::udp::resolver::query (*i, "24000"), [client_l] (boost::system::error_code const & ec, boost::asio::ip::udp::resolver::iterator i_a)
            {
                if (!ec)
                {
                    for (auto i (i_a), n (boost::asio::ip::udp::resolver::iterator {}); i != n; ++i)
                    {
                        client_l->network.send_keepalive (i->endpoint ());
                    }
                }
            });
        }
    });
}

rai::bootstrap_receiver::bootstrap_receiver (boost::asio::io_service & service_a, uint16_t port_a, rai::client & client_a) :
acceptor (service_a),
local (boost::asio::ip::tcp::endpoint (boost::asio::ip::address_v4::any (), port_a)),
service (service_a),
client (client_a)
{
}

void rai::bootstrap_receiver::start ()
{
    acceptor.open (local.protocol ());
    acceptor.set_option (boost::asio::ip::tcp::acceptor::reuse_address (true));
    acceptor.bind (local);
    acceptor.listen ();
    accept_connection ();
}

void rai::bootstrap_receiver::stop ()
{
    on = false;
    acceptor.close ();
}

void rai::bootstrap_receiver::accept_connection ()
{
    auto socket (std::make_shared <boost::asio::ip::tcp::socket> (service));
    acceptor.async_accept (*socket, [this, socket] (boost::system::error_code const & ec)
    {
        accept_action (ec, socket);
    });
}

void rai::bootstrap_receiver::accept_action (boost::system::error_code const & ec, std::shared_ptr <boost::asio::ip::tcp::socket> socket_a)
{
    if (!ec)
    {
        accept_connection ();
        auto connection (std::make_shared <rai::bootstrap_connection> (socket_a, client.shared ()));
        connection->receive ();
    }
    else
    {
        client.log.add (boost::str (boost::format ("Error while accepting bootstrap connections: %1%") % ec.message ()));
    }
}

rai::bootstrap_connection::bootstrap_connection (std::shared_ptr <boost::asio::ip::tcp::socket> socket_a, std::shared_ptr <rai::client> client_a) :
socket (socket_a),
client (client_a)
{
}

void rai::bootstrap_connection::receive ()
{
    auto this_l (shared_from_this ());
    boost::asio::async_read (*socket, boost::asio::buffer (receive_buffer.data (), 1), [this_l] (boost::system::error_code const & ec, size_t size_a)
    {
        this_l->receive_type_action (ec, size_a);
    });
}

void rai::bootstrap_connection::receive_type_action (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
        assert (size_a == 1);
        rai::bufferstream type_stream (receive_buffer.data (), size_a);
        rai::message_type type;
        read (type_stream, type);
        switch (type)
        {
            case rai::message_type::bulk_req:
            {
                auto this_l (shared_from_this ());
                boost::asio::async_read (*socket, boost::asio::buffer (receive_buffer.data () + 1, sizeof (rai::uint256_union) + sizeof (rai::uint256_union)), [this_l] (boost::system::error_code const & ec, size_t size_a)
                {
                    this_l->receive_bulk_req_action (ec, size_a);
                });
                break;
            }
			case rai::message_type::frontier_req:
			{
				auto this_l (shared_from_this ());
				boost::asio::async_read (*socket, boost::asio::buffer (receive_buffer.data () + 1, sizeof (rai::uint256_union) + sizeof (uint32_t) + sizeof (uint32_t)), [this_l] (boost::system::error_code const & ec, size_t size_a)
                {
                    this_l->receive_frontier_req_action (ec, size_a);
                });
				break;
			}
            default:
            {
                if (network_logging ())
                {
                    client->log.add (boost::str (boost::format ("Received invalid type from bootstrap connection %1%") % static_cast <uint8_t> (type)));
                }
                break;
            }
        }
    }
    else
    {
        if (network_logging ())
        {
            client->log.add (boost::str (boost::format ("Error while receiving type %1%") % ec.message ()));
        }
    }
}

void rai::bootstrap_connection::receive_bulk_req_action (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
        std::unique_ptr <rai::bulk_req> request (new rai::bulk_req);
        rai::bufferstream stream (receive_buffer.data (), sizeof (rai::message_type) + sizeof (rai::uint256_union) + sizeof (rai::uint256_union));
        auto error (request->deserialize (stream));
        if (!error)
        {
            receive ();
            if (network_logging ())
            {
                client->log.add (boost::str (boost::format ("Received bulk request for %1% down to %2%") % request->start.to_string () % request->end.to_string ()));
            }
			add_request (std::unique_ptr <rai::message> (request.release ()));
        }
    }
}

void rai::bootstrap_connection::receive_frontier_req_action (boost::system::error_code const & ec, size_t size_a)
{
	if (!ec)
	{
		std::unique_ptr <rai::frontier_req> request (new rai::frontier_req);
		rai::bufferstream stream (receive_buffer.data (), sizeof (rai::message_type) + sizeof (rai::uint256_union) + sizeof (uint32_t) + sizeof (uint32_t));
		auto error (request->deserialize (stream));
		if (!error)
		{
			receive ();
			if (network_logging ())
			{
				client->log.add (boost::str (boost::format ("Received frontier request for %1% with age %2%") % request->start.to_string () % request->age));
			}
			add_request (std::unique_ptr <rai::message> (request.release ()));
		}
	}
    else
    {
        if (network_logging ())
        {
            client->log.add (boost::str (boost::format ("Error sending receiving frontier request %1%") % ec.message ()));
        }
    }
}

void rai::bootstrap_connection::add_request (std::unique_ptr <rai::message> message_a)
{
	std::lock_guard <std::mutex> lock (mutex);
    auto start (requests.empty ());
	requests.push (std::move (message_a));
	if (start)
	{
		run_next ();
	}
}

void rai::bootstrap_connection::finish_request ()
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
    request_response_visitor (std::shared_ptr <rai::bootstrap_connection> connection_a) :
    connection (connection_a)
    {
    }
    void keepalive_req (rai::keepalive_req const &)
    {
        assert (false);
    }
    void keepalive_ack (rai::keepalive_ack const &)
    {
        assert (false);
    }
    void publish (rai::publish const &)
    {
        assert (false);
    }
    void confirm_req (rai::confirm_req const &)
    {
        assert (false);
    }
    void confirm_ack (rai::confirm_ack const &)
    {
        assert (false);
    }
    void confirm_unk (rai::confirm_unk const &)
    {
        assert (false);
    }
    void bulk_req (rai::bulk_req const &)
    {
        auto response (std::make_shared <rai::bulk_req_response> (connection, std::unique_ptr <rai::bulk_req> (static_cast <rai::bulk_req *> (connection->requests.front ().release ()))));
        response->send_next ();
    }
    void frontier_req (rai::frontier_req const &)
    {
        auto response (std::make_shared <rai::frontier_req_response> (connection, std::unique_ptr <rai::frontier_req> (static_cast <rai::frontier_req *> (connection->requests.front ().release ()))));
        response->send_next ();
    }
    std::shared_ptr <rai::bootstrap_connection> connection;
};
}

void rai::bootstrap_connection::run_next ()
{
	assert (!requests.empty ());
    request_response_visitor visitor (shared_from_this ());
    requests.front ()->visit (visitor);
}

void rai::bulk_req_response::set_current_end ()
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

void rai::bulk_req_response::send_next ()
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
            connection->client->log.add (boost::str (boost::format ("Sending block: %1%") % block->hash ().to_string ()));
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

std::unique_ptr <rai::block> rai::bulk_req_response::get_next ()
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

void rai::bulk_req_response::sent_action (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
        send_next ();
    }
}

void rai::bulk_req_response::send_finished ()
{
    send_buffer.clear ();
    send_buffer.push_back (static_cast <uint8_t> (rai::block_type::not_a_block));
    auto this_l (shared_from_this ());
    if (network_logging ())
    {
        connection->client->log.add ("Bulk sending finished");
    }
    async_write (*connection->socket, boost::asio::buffer (send_buffer.data (), 1), [this_l] (boost::system::error_code const & ec, size_t size_a)
    {
        this_l->no_block_sent (ec, size_a);
    });
}

void rai::bulk_req_response::no_block_sent (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
        assert (size_a == 1);
		connection->finish_request ();
    }
}

rai::account_iterator rai::block_store::latest_begin (rai::address const & address_a)
{
    rai::account_iterator result (*addresses, address_a);
    return result;
}

namespace
{
class request_visitor : public rai::message_visitor
{
public:
    request_visitor (std::shared_ptr <rai::bootstrap_initiator> connection_a) :
    connection (connection_a)
    {
    }
    void keepalive_req (rai::keepalive_req const &)
    {
        assert (false);
    }
    void keepalive_ack (rai::keepalive_ack const &)
    {
        assert (false);
    }
    void publish (rai::publish const &)
    {
        assert (false);
    }
    void confirm_req (rai::confirm_req const &)
    {
        assert (false);
    }
    void confirm_ack (rai::confirm_ack const &)
    {
        assert (false);
    }
    void confirm_unk (rai::confirm_unk const &)
    {
        assert (false);
    }
    void bulk_req (rai::bulk_req const &)
    {
        auto response (std::make_shared <rai::bulk_req_initiator> (connection, std::unique_ptr <rai::bulk_req> (static_cast <rai::bulk_req *> (connection->requests.front ().release ()))));
        response->receive_block ();
    }
    void frontier_req (rai::frontier_req const &)
    {
        auto response (std::make_shared <rai::frontier_req_initiator> (connection, std::unique_ptr <rai::frontier_req> (static_cast <rai::frontier_req *> (connection->requests.front ().release ()))));
        response->receive_frontier ();
    }
    std::shared_ptr <rai::bootstrap_initiator> connection;
};
}

rai::bootstrap_initiator::bootstrap_initiator (std::shared_ptr <rai::client> client_a, std::function <void ()> const & complete_action_a) :
client (client_a),
socket (client_a->network.service),
complete_action (complete_action_a)
{
}

void rai::bootstrap_initiator::run (boost::asio::ip::tcp::endpoint const & endpoint_a)
{
    if (network_logging ())
    {
        client->log.add (boost::str (boost::format ("Initiating bootstrap connection to %1%") % endpoint_a));
    }
    auto this_l (shared_from_this ());
    socket.async_connect (endpoint_a, [this_l] (boost::system::error_code const & ec)
    {
        this_l->connect_action (ec);
    });
}

void rai::bootstrap_initiator::connect_action (boost::system::error_code const & ec)
{
    if (!ec)
    {
        send_frontier_request ();
    }
    else
    {
        if (network_logging ())
        {
            client->log.add (boost::str (boost::format ("Error initiating bootstrap connection %1%") % ec.message ()));
        }
    }
}

void rai::bootstrap_initiator::send_frontier_request ()
{
    std::unique_ptr <rai::frontier_req> request (new rai::frontier_req);
    request->start.clear ();
    request->age = std::numeric_limits <decltype (request->age)>::max ();
    request->count = std::numeric_limits <decltype (request->age)>::max ();
    add_request (std::move (request));
}

void rai::bootstrap_initiator::sent_request (boost::system::error_code const & ec, size_t size_a)
{
    if (ec)
    {
        if (network_logging ())
        {
            client->log.add (boost::str (boost::format ("Error while sending bootstrap request %1%") % ec.message ()));
        }
    }
}

void rai::bootstrap_initiator::add_request (std::unique_ptr <rai::message> message_a)
{
    std::lock_guard <std::mutex> lock (mutex);
    send_buffer.clear ();
    {
        rai::vectorstream stream (send_buffer);
        message_a->serialize (stream);
    }
    auto startup (requests.empty ());
    requests.push (std::move (message_a));
    if (startup)
    {
        run_receiver ();
    }
    auto this_l (shared_from_this ());
    boost::asio::async_write (socket, boost::asio::buffer (send_buffer.data (), send_buffer.size ()), [this_l] (boost::system::error_code const & ec, size_t size_a)
    {
        this_l->sent_request (ec, size_a);
    });
}

void rai::bootstrap_initiator::run_receiver ()
{
    assert (!mutex.try_lock ());
    assert (requests.front () != nullptr);
    request_visitor visitor (shared_from_this ());
    requests.front ()->visit (visitor);
}

void rai::bootstrap_initiator::finish_request ()
{
    std::lock_guard <std::mutex> lock (mutex);
    assert (!requests.empty ());
    requests.pop ();
    if (!requests.empty ())
    {
        run_receiver ();
    }
}

void rai::bulk_req_initiator::receive_block ()
{
    auto this_l (shared_from_this ());
    boost::asio::async_read (connection->socket, boost::asio::buffer (receive_buffer.data (), 1), [this_l] (boost::system::error_code const & ec, size_t size_a)
    {
        this_l->received_type (ec, size_a);
    });
}

void rai::bulk_req_initiator::received_type (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
        auto this_l (shared_from_this ());
        rai::block_type type (static_cast <rai::block_type> (receive_buffer [0]));
        switch (type)
        {
            case rai::block_type::send:
            {
				boost::asio::async_read (connection->socket, boost::asio::buffer (receive_buffer.data () + 1, sizeof (rai::signature) + sizeof (rai::block_hash) + sizeof (rai::amount) + sizeof (rai::address)), [this_l] (boost::system::error_code const & ec, size_t size_a)
                {
                    this_l->received_block (ec, size_a);
                });
                break;
            }
            case rai::block_type::receive:
            {
				boost::asio::async_read (connection->socket, boost::asio::buffer (receive_buffer.data () + 1, sizeof (rai::signature) + sizeof (rai::block_hash) + sizeof (rai::block_hash)), [this_l] (boost::system::error_code const & ec, size_t size_a)
                {
                    this_l->received_block (ec, size_a);
                });
                break;
            }
            case rai::block_type::open:
            {
				boost::asio::async_read (connection->socket, boost::asio::buffer (receive_buffer.data () + 1, sizeof (rai::signature) + sizeof (rai::block_hash) + sizeof (rai::address)), [this_l] (boost::system::error_code const & ec, size_t size_a)
                {
                    this_l->received_block (ec, size_a);
                });
                break;
            }
            case rai::block_type::change:
            {
				boost::asio::async_read (connection->socket, boost::asio::buffer (receive_buffer.data () + 1, sizeof (rai::signature) + sizeof (rai::block_hash) + sizeof (rai::address)), [this_l] (boost::system::error_code const & ec, size_t size_a)
                {
                    this_l->received_block (ec, size_a);
                });
                break;
            }
            case rai::block_type::not_a_block:
            {
                auto error (process_end ());
                if (error)
                {
                    connection->client->log.add ("Error processing end_block");
                }
                break;
            }
            default:
            {
                connection->client->log.add ("Unknown type received as block type");
                break;
            }
        }
    }
    else
    {
        connection->client->log.add (boost::str (boost::format ("Error receiving block type %1%") % ec.message ()));
    }
}

namespace
{
class observed_visitor : public rai::block_visitor
{
public:
    observed_visitor () :
    address (0)
    {
    }
    void send_block (rai::send_block const & block_a)
    {
        address = block_a.hashables.destination;
    }
    void receive_block (rai::receive_block const &)
    {
    }
    void open_block (rai::open_block const &)
    {
    }
    void change_block (rai::change_block const &)
    {
    }
    rai::address address;
};
}

bool rai::bulk_req_initiator::process_end ()
{
    bool result;
    if (expecting == request->end)
    {
        rai::process_result processing (rai::process_result::progress);
        std::unique_ptr <rai::block> block;
        do
        {
            block = connection->client->store.bootstrap_get (expecting);
            if (block != nullptr)
            {
                processing = connection->client->processor.process_receive (*block);
                expecting = block->hash ();
            }
        } while (block != nullptr && processing == rai::process_result::progress);
        result = processing != rai::process_result::progress;
    }
    else if (expecting == request->start)
    {
        result = false;
    }
    else
    {
        result = true;
    }
    connection->finish_request ();
    return result;
}

rai::block_hash rai::genesis::hash () const
{
    return open.hash ();
}

void rai::bulk_req_initiator::received_block (boost::system::error_code const & ec, size_t size_a)
{
	if (!ec)
	{
		rai::bufferstream stream (receive_buffer.data (), 1 + size_a);
		auto block (rai::deserialize_block (stream));
		if (block != nullptr)
		{
			auto error (process_block (*block));
			if (!error)
			{
				receive_block ();
			}
		}
	}
}

bool rai::bulk_req_initiator::process_block (rai::block const & block)
{
    assert (!connection->requests.empty ());
    bool result;
    auto hash (block.hash ());
    if (network_logging ())
    {
        connection->client->log.add (boost::str (boost::format ("Received block: %1%") % hash.to_string ()));
    }
    if (expecting != request->end && (expecting == request->start || hash == expecting))
    {
        auto previous (block.previous ());
        connection->client->store.bootstrap_put (previous, block);
        expecting = previous;
        if (network_logging ())
        {
            connection->client->log.add (boost::str (boost::format ("Expecting: %1%") % expecting.to_string ()));
        }
        result = false;
    }
    else
    {
		if (network_logging ())
		{
            connection->client->log.add (boost::str (boost::format ("Block hash: %1% did not match expecting %1%") % expecting.to_string ()));
		}
        result = true;
    }
    return result;
}

bool rai::block_store::block_exists (rai::block_hash const & hash_a)
{
    bool result;
    std::unique_ptr <leveldb::Iterator> iterator (blocks->NewIterator (leveldb::ReadOptions ()));
    iterator->Seek (leveldb::Slice (hash_a.chars.data (), hash_a.chars.size ()));
    if (iterator->Valid ())
    {
        rai::uint256_union hash;
        hash = iterator->key ();
        if (hash == hash_a)
        {
            result = true;
        }
        else
        {
            result = false;
        }
    }
    else
    {
        result = false;
    }
    return result;
}

void rai::block_store::bootstrap_put (rai::block_hash const & hash_a, rai::block const & block_a)
{
    std::vector <uint8_t> vector;
    {
        rai::vectorstream stream (vector);
        rai::serialize_block (stream, block_a);
    }
    auto status (bootstrap->Put (leveldb::WriteOptions (), leveldb::Slice (hash_a.chars.data (), hash_a.chars.size ()), leveldb::Slice (reinterpret_cast <char const *> (vector.data ()), vector.size ())));
    assert (status.ok () | status.IsNotFound ());
}

std::unique_ptr <rai::block> rai::block_store::bootstrap_get (rai::block_hash const & hash_a)
{
    std::string value;
    auto status (bootstrap->Get (leveldb::ReadOptions (), leveldb::Slice (hash_a.chars.data (), hash_a.chars.size ()), &value));
    assert (status.ok () || status.IsNotFound ());
    std::unique_ptr <rai::block> result;
    if (status.ok ())
    {
        rai::bufferstream stream (reinterpret_cast <uint8_t const *> (value.data ()), value.size ());
        result = rai::deserialize_block (stream);
        assert (result != nullptr);
    }
    return result;
}

void rai::block_store::bootstrap_del (rai::block_hash const & hash_a)
{
    auto status (bootstrap->Delete (leveldb::WriteOptions (), leveldb::Slice (hash_a.chars.data (), hash_a.chars.size ())));
    assert (status.ok ());
}

rai::endpoint rai::network::endpoint ()
{
    return rai::endpoint (boost::asio::ip::address_v4::loopback (), socket.local_endpoint ().port ());
}

boost::asio::ip::tcp::endpoint rai::bootstrap_receiver::endpoint ()
{
    return boost::asio::ip::tcp::endpoint (boost::asio::ip::address_v4::loopback (), local.port ());
}

rai::bootstrap_initiator::~bootstrap_initiator ()
{
    complete_action ();
    if (network_logging ())
    {
        client->log.add ("Exiting bootstrap processor");
    }
}

rai::bootstrap_connection::~bootstrap_connection ()
{
    if (network_logging ())
    {
        client->log.add ("Exiting bootstrap connection");
    }
}

void rai::peer_container::random_fill (std::array <rai::endpoint, 24> & target_a)
{
    auto peers (list ());
    while (peers.size () > target_a.size ())
    {
        auto index (random_pool.GenerateWord32 (0, peers.size ()));
        peers [index] = peers [peers.size () - 1];
        peers.pop_back ();
    }
    auto k (target_a.begin ());
    for (auto i (peers.begin ()), j (peers.begin () + std::min (peers.size (), target_a.size ())); i != j; ++i, ++k)
    {
        *k = i->endpoint;
    }
    std::fill (target_a.begin () + std::min (peers.size (), target_a.size ()), target_a.end (), rai::endpoint ());
}

void rai::processor::ongoing_keepalive ()
{
    auto peers (client.peers.purge_list (std::chrono::system_clock::now () - cutoff));
    for (auto i (peers.begin ()), j (peers.end ()); i != j && std::chrono::system_clock::now () - i->last_attempt > period; ++i)
    {
        client.network.send_keepalive (i->endpoint);
    }
    client.service.add (std::chrono::system_clock::now () + period, [this] () { ongoing_keepalive ();});
}

std::vector <rai::peer_information> rai::peer_container::purge_list (std::chrono::system_clock::time_point const & cutoff)
{
    std::unique_lock <std::mutex> lock (mutex);
    auto pivot (peers.get <1> ().lower_bound (cutoff));
    std::vector <rai::peer_information> result (pivot, peers.get <1> ().end ());
    peers.get <1> ().erase (peers.get <1> ().begin (), pivot);
    return result;
}

size_t rai::peer_container::size ()
{
    std::unique_lock <std::mutex> lock (mutex);
    return peers.size ();
}

bool rai::peer_container::empty ()
{
    return size () == 0;
}

bool rai::peer_container::contacting_peer (rai::endpoint const & endpoint_a)
{
	auto result (rai::reserved_address (endpoint_a));
	if (!result)
	{
		if (endpoint_a != self)
		{
			std::unique_lock <std::mutex> lock (mutex);
			auto existing (peers.find (endpoint_a));
			if (existing != peers.end ())
			{
				result = true;
			}
			else
			{
				peers.insert ({endpoint_a, std::chrono::system_clock::time_point (), std::chrono::system_clock::now ()});
			}
		}
	}
	return result;
}

bool rai::reserved_address (rai::endpoint const & endpoint_a)
{
	auto bytes (endpoint_a.address ().to_v4().to_ulong ());
	auto result (false);
	if (bytes <= 0x00ffffffu)
	{
		result = true;
	}
	else if (bytes >= 0xc0000200ul && bytes <= 0xc00002fful)
	{
		result = true;
	}
	else if (bytes >= 0xc6336400ul && bytes <= 0xc63364fful)
	{
		result = true;
	}
	else if (bytes >= 0xcb007100ul && bytes <= 0xcb0071fful)
	{
		result = true;
	}
	else if (bytes >= 0xe9fc0000ul && bytes <= 0xe9fc00fful)
	{
		result = true;
	}
	else if (bytes >= 0xf0000000ul)
	{
		result = true;
	}
	return result;
}

rai::peer_container::peer_container (rai::endpoint const & self_a) :
self (self_a)
{
}

void rai::log::add (std::string const & string_a)
{
    if (log_to_cerr ())
    {
        std::cerr << string_a << std::endl;
    }
    items.push_back (std::make_pair (std::chrono::system_clock::now (), string_a));
}

void rai::log::dump_cerr ()
{
    for (auto & i: items)
    {
        std::cerr << i.first << ' ' << i.second << std::endl;
    }
}

rai::log::log () :
items (1024)
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
    std::unique_lock <std::mutex> lock (mutex);
    auto do_send (sends.empty ());
    sends.push (std::make_tuple (data_a, size_a, endpoint_a, callback_a));
    if (do_send)
    {
        if (network_packet_logging ())
        {
            client.log.add ("Sending packet");
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
        client.log.add ("Packet send complete");
    }
    std::tuple <uint8_t const *, size_t, rai::endpoint, std::function <void (boost::system::error_code const &, size_t)>> self;
    {
        std::unique_lock <std::mutex> lock (mutex);
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
                    client.log.add ("Sending packet");
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

rai::bulk_req_response::bulk_req_response (std::shared_ptr <rai::bootstrap_connection> const & connection_a, std::unique_ptr <rai::bulk_req> request_a) :
connection (connection_a),
request (std::move (request_a))
{
    set_current_end ();
}

rai::frontier_req_response::frontier_req_response (std::shared_ptr <rai::bootstrap_connection> const & connection_a, std::unique_ptr <rai::frontier_req> request_a) :
iterator (connection_a->client->store.latest_begin (request_a->start)),
connection (connection_a),
request (std::move (request_a))
{
    skip_old ();
}

void rai::frontier_req_response::skip_old ()
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

void rai::frontier_req_response::send_next ()
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
            connection->client->log.add (boost::str (boost::format ("Sending frontier for %1% %2%") % pair.first.to_string () % pair.second.to_string ()));
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

void rai::frontier_req_response::send_finished ()
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
        connection->client->log.add ("Frontier sending finished");
    }
    async_write (*connection->socket, boost::asio::buffer (send_buffer.data (), send_buffer.size ()), [this_l] (boost::system::error_code const & ec, size_t size_a)
    {
        this_l->no_block_sent (ec, size_a);
    });
}

void rai::frontier_req_response::no_block_sent (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
		connection->finish_request ();
    }
    else
    {
        if (network_logging ())
        {
            connection->client->log.add (boost::str (boost::format ("Error sending frontier finish %1%") % ec.message ()));
        }
    }
}

void rai::frontier_req_response::sent_action (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
        send_next ();
    }
    else
    {
        if (network_logging ())
        {
            connection->client->log.add (boost::str (boost::format ("Error sending frontier pair %1%") % ec.message ()));
        }
    }
}

std::pair <rai::uint256_union, rai::uint256_union> rai::frontier_req_response::get_next ()
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

bool rai::frontier_req::deserialize (rai::stream & stream_a)
{
    rai::message_type type;
    auto result (read (stream_a, type));
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
    write (stream_a, rai::message_type::frontier_req);
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

rai::bulk_req_initiator::bulk_req_initiator (std::shared_ptr <rai::bootstrap_initiator> const & connection_a, std::unique_ptr <rai::bulk_req> request_a) :
request (std::move (request_a)),
expecting (request->start),
connection (connection_a)
{
    assert (!connection_a->requests.empty ());
    assert (connection_a->requests.front () == nullptr);
}

rai::bulk_req_initiator::~bulk_req_initiator ()
{
    if (network_logging ())
    {
        connection->client->log.add ("Exiting bulk_req initiator");
    }
}

rai::frontier_req_initiator::frontier_req_initiator (std::shared_ptr <rai::bootstrap_initiator> const & connection_a, std::unique_ptr <rai::frontier_req> request_a) :
request (std::move (request_a)),
connection (connection_a)
{
}

rai::frontier_req_initiator::~frontier_req_initiator ()
{
    if (network_logging ())
    {
        connection->client->log.add ("Exiting frontier_req initiator");
    }
}

void rai::frontier_req_initiator::receive_frontier ()
{
    auto this_l (shared_from_this ());
    boost::asio::async_read (connection->socket, boost::asio::buffer (receive_buffer.data (), sizeof (rai::uint256_union) + sizeof (rai::uint256_union)), [this_l] (boost::system::error_code const & ec, size_t size_a)
    {
        this_l->received_frontier (ec, size_a);
    });
}

void rai::frontier_req_initiator::received_frontier (boost::system::error_code const & ec, size_t size_a)
{
    if (!ec)
    {
        assert (size_a == sizeof (rai::uint256_union) + sizeof (rai::uint256_union));
        rai::address address;
        rai::bufferstream address_stream (receive_buffer.data (), sizeof (rai::uint256_union));
        auto error1 (rai::read (address_stream, address));
        assert (!error1);
        rai::block_hash latest;
        rai::bufferstream latest_stream (receive_buffer.data () + sizeof (rai::uint256_union), sizeof (rai::uint256_union));
        auto error2 (rai::read (latest_stream, latest));
        assert (!error2);
        if (!address.is_zero ())
        {
            rai::frontier frontier;
            auto unknown (connection->client->store.latest_get (address, frontier));
            if (unknown)
            {
                std::unique_ptr <rai::bulk_req> request (new rai::bulk_req);
                request->start = address;
                request->end.clear ();
                connection->add_request (std::move (request));
            }
            else
            {
                auto exists (connection->client->store.block_exists (latest));
                if (!exists)
                {
                    std::unique_ptr <rai::bulk_req> request (new rai::bulk_req);
                    request->start = address;
                    request->end = frontier.hash;
                    connection->add_request (std::move (request));
                }
            }
            receive_frontier ();
        }
        else
        {
            connection->finish_request ();
        }
    }
    else
    {
        if (network_logging ())
        {
            connection->client->log.add (boost::str (boost::format ("Error while receiving frontier %1%") % ec.message ()));
        }
    }
}

void rai::block_store::checksum_put (uint64_t prefix, uint8_t mask, rai::uint256_union const & hash_a)
{
    assert ((prefix & 0xff) == 0);
    uint64_t key (prefix | mask);
    auto status (checksum->Put (leveldb::WriteOptions (), leveldb::Slice (reinterpret_cast <char const *> (&key), sizeof (uint64_t)), leveldb::Slice (hash_a.chars.data (), hash_a.chars.size ())));
    assert (status.ok ());
}

bool rai::block_store::checksum_get (uint64_t prefix, uint8_t mask, rai::uint256_union & hash_a)
{
    assert ((prefix & 0xff) == 0);
    std::string value;
    uint64_t key (prefix | mask);
    auto status (checksum->Get (leveldb::ReadOptions (), leveldb::Slice (reinterpret_cast <char const *> (&key), sizeof (uint64_t)), &value));
    assert (status.ok () || status.IsNotFound ());
    bool result;
    if (status.ok ())
    {
        result = false;
        rai::bufferstream stream (reinterpret_cast <uint8_t const *> (value.data ()), value.size ());
        auto error (rai::read (stream, hash_a));
        assert (!error);
    }
    else
    {
        result = true;
    }
    return result;
}

void rai::block_store::checksum_del (uint64_t prefix, uint8_t mask)
{
    assert ((prefix & 0xff) == 0);
    uint64_t key (prefix | mask);
    checksum->Delete (leveldb::WriteOptions (), leveldb::Slice (reinterpret_cast <char const *> (&key), sizeof (uint64_t)));
}

bool rai::keepalive_ack::operator == (rai::keepalive_ack const & other_a) const
{
	return (peers == other_a.peers) && (checksum == other_a.checksum);
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
    rai::uint256_t balance (client_a.balance ());
    std::string balance_text (balance.convert_to <std::string> ());
    rai::uint128_union random_amount;
    random_pool.GenerateBlock (random_amount.bytes.data (), sizeof (random_amount.bytes));
    auto result (((rai::uint256_t {random_amount.number ()} * balance) / rai::uint256_t {std::numeric_limits <rai::uint128_t>::max ()}).convert_to <rai::uint128_t> ());
    std::string text (result.convert_to <std::string> ());
    return result;
}

void rai::system::generate_send_existing (rai::client & client_a)
{
    rai::address account;
    random_pool.GenerateBlock (account.bytes.data (), sizeof (account.bytes));
    rai::account_iterator entry (client_a.store.latest_begin (account));
    if (entry == client_a.store.latest_end ())
    {
        entry = client_a.store.latest_begin ();
    }
    assert (entry != client_a.store.latest_end ());
    client_a.send (entry->first, get_random_amount (client_a));
}

void rai::system::generate_send_new (rai::client & client_a)
{
    rai::keypair key;
    client_a.wallet.insert (key.prv);
    client_a.send (key.pub, get_random_amount (client_a));
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

rai::uint256_t rai::client::balance ()
{
    rai::uint256_t result;
    for (auto i (wallet.begin ()), n (wallet.end ()); i !=  n; ++i)
    {
        auto pub (i->first);
        auto account_balance (ledger.account_balance (pub));
        result += account_balance;
    }
    return result;
}

rai::transactions::transactions (rai::client & client_a) :
client (client_a)
{
}

bool rai::transactions::receive (rai::send_block const & send_a, rai::private_key const & prv_a, rai::address const & representative_a)
{
    std::lock_guard <std::mutex> lock (mutex);
    auto hash (send_a.hash ());
    bool result;
    if (client.ledger.store.pending_exists (hash))
    {
        rai::frontier frontier;
        auto new_address (client.ledger.store.latest_get (send_a.hashables.destination, frontier));
        if (new_address)
        {
            auto open (new rai::open_block);
            open->hashables.source = hash;
            open->hashables.representative = representative_a;
            rai::sign_message (prv_a, send_a.hashables.destination, open->hash (), open->signature);
            client.processor.process_receive_republish (std::unique_ptr <rai::block> (open), rai::endpoint {});
        }
        else
        {
            auto receive (new rai::receive_block);
            receive->hashables.previous = frontier.hash;
            receive->hashables.source = hash;
            rai::sign_message (prv_a, send_a.hashables.destination, receive->hash (), receive->signature);
            client.processor.process_receive_republish (std::unique_ptr <rai::block> (receive), rai::endpoint {});
        }
        result = false;
    }
    else
    {
        result = true;
        // Ledger doesn't have this marked as available to receive anymore
    }
    return result;
}

bool rai::transactions::send (rai::address const & address_a, rai::uint128_t const & amount_a)
{
    std::lock_guard <std::mutex> lock (mutex);
    std::vector <std::unique_ptr <rai::send_block>> blocks;
    auto result (!client.wallet.valid_password ());
    if (!result)
    {
        result = client.wallet.generate_send (client.ledger, address_a, amount_a, blocks);
        if (!result)
        {
            for (auto i (blocks.begin ()), j (blocks.end ()); i != j; ++i)
            {
                client.processor.process_receive_republish (std::move (*i), rai::endpoint {});
            }
        }
    }
    else
    {
        client.log.add ("Wallet key is invalid");
    }
    return result;
}

rai::election::election (std::shared_ptr <rai::client> client_a, rai::block const & block_a) :
votes (client_a->ledger, block_a),
client (client_a),
last_vote (std::chrono::system_clock::now ()),
confirmed (false)
{
    assert (client_a->store.block_exists (block_a.hash ()));
    rai::keypair anonymous;
    rai::vote vote_l;
    vote_l.address = anonymous.pub;
    vote_l.sequence = 0;
    vote_l.block = block_a.clone ();
    rai::sign_message (anonymous.prv, anonymous.pub, vote_l.hash (), vote_l.signature);
    vote (vote_l);
}

void rai::election::start ()
{
	client->representative_vote (*this, *votes.last_winner);
    if (client->is_representative ())
    {
        announce_vote ();
    }
    auto client_l (client);
    auto root_l (votes.root);
    auto destructable (std::make_shared <rai::destructable> ([client_l, root_l] () {client_l->conflicts.stop (root_l);}));
    timeout_action (destructable);
}

rai::destructable::destructable (std::function <void ()> operation_a) :
operation (operation_a)
{
}

rai::destructable::~destructable ()
{
    operation ();
}

void rai::election::timeout_action (std::shared_ptr <rai::destructable> destructable_a)
{
    auto now (std::chrono::system_clock::now ());
    if (now - last_vote < std::chrono::seconds (15))
    {
        auto this_l (shared_from_this ());
        client->service.add (now + std::chrono::seconds (15), [this_l, destructable_a] () {this_l->timeout_action (destructable_a);});
    }
}

rai::uint256_t rai::election::uncontested_threshold ()
{
    return client->ledger.supply () / 2;
}

rai::uint256_t rai::election::contested_threshold ()
{
    return (client->ledger.supply () / 16) * 15;
}

void rai::conflicts::start (rai::block const & block_a, bool request_a)
{
    std::lock_guard <std::mutex> lock (mutex);
    auto root (client.store.root (block_a));
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

void rai::conflicts::update (rai::vote const & vote_a)
{
    std::lock_guard <std::mutex> lock (mutex);
    auto existing (roots.find (client.store.root (*vote_a.block)));
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

namespace
{
class network_message_visitor : public rai::message_visitor
{
public:
	network_message_visitor (rai::client & client_a, rai::endpoint const & sender_a, bool known_peer_a) :
	client (client_a),
	sender (sender_a),
	known_peer (known_peer_a)
	{
	}
	void keepalive_req (rai::keepalive_req const & message_a) override
	{
		if (network_keepalive_logging ())
		{
			client.log.add (boost::str (boost::format ("Received keepalive req from %1%") % sender));
		}
		rai::keepalive_ack ack_message;
		client.peers.random_fill (ack_message.peers);
		ack_message.checksum = client.ledger.checksum (0, std::numeric_limits <rai::uint256_t>::max ());
		std::shared_ptr <std::vector <uint8_t>> ack_bytes (new std::vector <uint8_t>);
		{
			rai::vectorstream stream (*ack_bytes);
			ack_message.serialize (stream);
		}
		auto & client_l (client);
		client.network.send_buffer (ack_bytes->data (), ack_bytes->size (), sender, [ack_bytes, &client_l] (boost::system::error_code const & error, size_t size_a)
		{
			if (network_logging ())
			{
				if (error)
				{
					client_l.log.add (boost::str (boost::format ("Error sending keepalive ack: %1%") % error.message ()));
				}
			}
		});
		client.network.merge_peers (message_a.peers);
		if (network_keepalive_logging ())
		{
			client.log.add (boost::str (boost::format ("Sending keepalive ack to %1%") % sender));
		}
	}
	void keepalive_ack (rai::keepalive_ack const & message_a) override
	{
		if (network_keepalive_logging ())
		{
			client.log.add (boost::str (boost::format ("Received keepalive ack from %1%") % sender));
		}
		client.network.merge_peers (message_a.peers);
		client.peers.incoming_from_peer (sender);
		if (!known_peer && message_a.checksum != client.ledger.checksum (0, std::numeric_limits <rai::uint256_t>::max ()))
		{
			client.processor.bootstrap (rai::tcp_endpoint (sender.address (), sender.port ()),
										[] ()
										{
										});
		}
	}
	void publish (rai::publish const & message_a) override
	{
		if (network_message_logging ())
		{
			client.log.add (boost::str (boost::format ("Received publish req from %1%") % sender));
		}
		client.processor.process_receive_republish (message_a.block->clone (), sender);
	}
	void confirm_req (rai::confirm_req const & message_a) override
	{
		if (network_message_logging ())
		{
			client.log.add (boost::str (boost::format ("Received confirm req from %1%") % sender));
		}
        client.processor.process_receive_republish (message_a.block->clone (), sender);
        if (client.store.block_exists (message_a.block->hash ()))
        {
            client.processor.process_confirmation (*message_a.block, sender);
		}
	}
	void confirm_ack (rai::confirm_ack const & message_a) override
	{
		if (network_message_logging ())
		{
			client.log.add (boost::str (boost::format ("Received Confirm from %1%") % sender));
		}
        client.processor.process_receive_republish (message_a.vote.block->clone (), sender);
        client.conflicts.update (message_a.vote);
	}
	void confirm_unk (rai::confirm_unk const &) override
	{
		assert (false);
	}
	void bulk_req (rai::bulk_req const &) override
	{
		assert (false);
	}
	void frontier_req (rai::frontier_req const &) override
	{
		assert (false);
	}
	rai::client & client;
	rai::endpoint sender;
	bool known_peer;
};
}

void rai::processor::process_message (rai::message & message_a, rai::endpoint const & endpoint_a, bool known_peer_a)
{
	network_message_visitor visitor (client, endpoint_a, known_peer_a);
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
        if (!client.wallet.fetch (block_a.hashables.destination, prv))
        {
            auto error (client.transactions.receive (block_a, prv, client.representative));
            prv.bytes.fill (0);
            assert (!error);
        }
        else
        {
            // Wallet doesn't contain key for this destination or couldn't decrypt
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

bool rai::client::is_representative ()
{
    return wallet.find (representative) != wallet.end ();
}

void rai::client::representative_vote (rai::election & election_a, rai::block const & block_a)
{
	if (is_representative ())
	{
        rai::private_key prv;
        rai::vote vote_l;
        vote_l.address = representative;
        vote_l.sequence = 0;
        vote_l.block = block_a.clone ();
		wallet.fetch (representative, prv);
        rai::sign_message (prv, representative, vote_l.hash (), vote_l.signature);
        prv.clear ();
        election_a.vote (vote_l);
	}
}

rai::uint256_union rai::wallet::check ()
{
    std::string check;
    auto status (handle->Get (leveldb::ReadOptions (), leveldb::Slice (wallet::check_special.chars.data (), wallet::check_special.chars.size ()), &check));
    assert (status.ok ());
    rai::uint256_union result;
    assert (check.size () == sizeof (rai::uint256_union));
    std::copy (check.begin (), check.end (), result.chars.begin ());
    return result;
}

rai::uint256_union rai::wallet::salt ()
{
    std::string salt_string;
    auto status (handle->Get (leveldb::ReadOptions (), leveldb::Slice (wallet::salt_special.chars.data (), wallet::salt_special.chars.size ()), &salt_string));
    assert (status.ok ());
    rai::uint256_union result;
    assert (salt_string.size () == result.chars.size ());
    std::copy (salt_string.data (), salt_string.data () + salt_string.size (), result.chars.data ());
    return result;
}

rai::uint256_union rai::wallet::wallet_key ()
{
    std::string encrypted_wallet_key;
    auto status (handle->Get (leveldb::ReadOptions (), leveldb::Slice (wallet::wallet_key_special.chars.data (), wallet::wallet_key_special.chars.size ()), &encrypted_wallet_key));
    assert (status.ok ());
    assert (encrypted_wallet_key.size () == sizeof (rai::uint256_union));
    rai::uint256_union encrypted_key;
    std::copy (encrypted_wallet_key.begin (), encrypted_wallet_key.end (), encrypted_key.chars.begin ());
    auto password_l (password.value ());
    auto result (encrypted_key.prv (password_l, salt ().owords [0]));
    password_l.clear ();
    return result;
}

bool rai::wallet::valid_password ()
{
    rai::uint256_union zero;
    zero.clear ();
    auto wallet_key_l (wallet_key ());
    rai::uint256_union check_l (zero, wallet_key_l, salt ().owords [0]);
    wallet_key_l.clear ();
    return check () == check_l;
}

bool rai::transactions::rekey (std::string const & password_a)
{
	std::lock_guard <std::mutex> lock (mutex);
    return client.wallet.rekey (password_a);
}

bool rai::wallet::rekey (std::string const & password_a)
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
        auto status1 (handle->Put (leveldb::WriteOptions (), leveldb::Slice (wallet::wallet_key_special.chars.data (), wallet::wallet_key_special.chars.size ()), leveldb::Slice (encrypted.chars.data (), encrypted.chars.size ())));
        wallet_key_l.clear ();
        assert (status1.ok ());
    }
    else
    {
        result = true;
    }
    return result;
}

namespace
{
class kdf
{
public:
    size_t static constexpr rai_full_work = 8 * 1024 * 1024;
    size_t static constexpr rai_test_work = 8 * 1024;
    size_t static constexpr entry_count = WORK_FACTOR;
    kdf (std::string const & password_a, rai::uint256_union const & salt_a)
    {
        auto entries (entry_count);
        std::unique_ptr <uint64_t []> allocation (new uint64_t [entries] ());
        xorshift1024star rng;
        rng.s.fill (0);
        rai::uint256_union password_l;
        CryptoPP::SHA3 hash1 (password_l.bytes.size ());
        hash1.Update (reinterpret_cast <uint8_t const *> (password_a.data ()), password_a.size ());
        hash1.Final (password_l.bytes.data ());
        rng.s [0] = password_l.qwords [0];
        rng.s [1] = password_l.qwords [1];
        rng.s [2] = password_l.qwords [2];
        rng.s [3] = password_l.qwords [3];
        rng.s [4] = salt_a.qwords [0];
        rng.s [5] = salt_a.qwords [1];
        rng.s [6] = salt_a.qwords [2];
        rng.s [7] = salt_a.qwords [3];
        size_t mask (entries - 1);
        auto previous (rng.next ());
        for (auto i (0u); i != entry_count; ++i)
        {
            auto next (rng.next ());
            allocation [static_cast <size_t> (previous & mask)] = next;
            previous = next;
        }
        CryptoPP::SHA3 hash2 (key.bytes.size ());
        hash2.Update (reinterpret_cast <uint8_t const *> (allocation.get ()), entries * sizeof (uint64_t));
        hash2.Final (key.bytes.data ());
    }
    rai::uint256_union key;
};
}

rai::uint256_union rai::wallet::derive_key (std::string const & password_a)
{
    kdf key (password_a, salt ());
    return key.key;
}

bool rai::confirm_req::operator == (rai::confirm_req const & other_a) const
{
    return work == other_a.work && *block == *other_a.block;
}

bool rai::publish::operator == (rai::publish const & other_a) const
{
    return work == other_a.work && *block == *other_a.block;
}

rai::work::work () :
entry_requirement (8 * 1024),
iteration_requirement (8 * 1024)
{
    auto error (threshold_requirement.decode_hex ("ff00000000000000000000000000000000000000000000000000000000000000"));
    //auto error (threshold_requirement.decode_hex ("8000000000000000000000000000000000000000000000000000000000000000"));
    assert (!error);
    entries.resize (entry_requirement);
}

rai::uint256_union rai::work::generate (rai::uint256_union const & seed, rai::uint256_union const & nonce)
{
    auto mask (entries.size () - 1);
	xorshift1024star rng;
    rng.s.fill (0);
    rng.s [0] = seed.qwords [0];
    rng.s [1] = seed.qwords [1];
    rng.s [2] = seed.qwords [2];
    rng.s [3] = seed.qwords [3];
    rng.s [4] = nonce.qwords [0];
    rng.s [5] = nonce.qwords [1];
    rng.s [6] = nonce.qwords [2];
    rng.s [7] = nonce.qwords [3];
    std::fill (entries.begin (), entries.end (), 0);
	for (auto i (0u), n (iteration_requirement); i != n; ++i)
	{
		auto next (rng.next ());
		auto index (next & mask);
		entries [index] = next;
	}
    CryptoPP::SHA3 hash (32);
    for (auto & i: entries)
    {
		auto address (&i);
        hash.Update (reinterpret_cast <uint8_t *> (address), sizeof (uint64_t));
    }
    rai::uint256_union result;
    hash.Final (result.bytes.data ());
    return result;
}

rai::uint256_union rai::work::create (rai::uint256_union const & seed)
{
    xorshift1024star rng;
    rng.s.fill (0);
    rng.s [0] = 1; // No seed here, we're not securing anything, s just can't be 0 per the spec
    rai::uint256_union result;
    rai::uint256_union value;
    do
    {
        for (auto i (0); i < result.qwords.size (); ++i)
        {
            result.qwords [i] = rng.next ();
        }
        value = generate (seed, result);
    } while (value < threshold_requirement);
    return result;
}

bool rai::work::validate (rai::uint256_union const & seed, rai::uint256_union const & nonce)
{
    auto value (generate (seed, nonce));
    return value < threshold_requirement;
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