#pragma once
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/asio.hpp>
#include <boost/filesystem.hpp>
#include <boost/network/include/http/server.hpp>
#include <boost/network/utils/thread_pool.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/circular_buffer.hpp>

#include <ed25519-donna/ed25519.h>
#include <cryptopp/sha3.h>

#include <leveldb/db.h>

#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>

namespace CryptoPP
{
    class SHA3;
}

std::ostream & operator << (std::ostream &, std::chrono::system_clock::time_point const &);
namespace rai {
    using stream = std::basic_streambuf <uint8_t>;
    using bufferstream = boost::iostreams::stream_buffer <boost::iostreams::basic_array_source <uint8_t>>;
    using vectorstream = boost::iostreams::stream_buffer <boost::iostreams::back_insert_device <std::vector <uint8_t>>>;
    template <typename T>
    bool read (rai::stream & stream_a, T & value)
    {
        auto amount_read (stream_a.sgetn (reinterpret_cast <uint8_t *> (&value), sizeof (value)));
        return amount_read != sizeof (value);
    }
    template <typename T>
    void write (rai::stream & stream_a, T const & value)
    {
        auto amount_written (stream_a.sputn (reinterpret_cast <uint8_t const *> (&value), sizeof (value)));
        assert (amount_written == sizeof (value));
    }
    using uint128_t = boost::multiprecision::uint128_t;
    using uint256_t = boost::multiprecision::uint256_t;
    using uint512_t = boost::multiprecision::uint512_t;
    union uint128_union
    {
    public:
        uint128_union () = default;
        uint128_union (rai::uint128_union const &) = default;
        uint128_union (rai::uint128_t const &);
        std::array <uint8_t, 16> bytes;
        std::array <uint64_t, 2> qwords;
    };
    union uint256_union
    {
        uint256_union () = default;
        uint256_union (uint64_t);
        uint256_union (rai::uint256_t const &);
        uint256_union (std::string const &);
        uint256_union (rai::uint256_union const &, rai::uint256_union const &, uint128_union const &);
        uint256_union prv (uint256_union const &, uint128_union const &) const;
        uint256_union & operator = (leveldb::Slice const &);
        uint256_union & operator ^= (rai::uint256_union const &);
        uint256_union operator ^ (rai::uint256_union const &) const;
        bool operator == (rai::uint256_union const &) const;
        bool operator != (rai::uint256_union const &) const;
        bool operator < (rai::uint256_union const &) const;
        void encode_hex (std::string &) const;
        bool decode_hex (std::string const &);
        void encode_dec (std::string &) const;
        bool decode_dec (std::string const &);
        void encode_base58check (std::string &) const;
        bool decode_base58check (std::string const &);
        void serialize (rai::stream &) const;
        bool deserialize (rai::stream &);
        std::array <uint8_t, 32> bytes;
        std::array <char, 32> chars;
        std::array <uint64_t, 4> qwords;
        std::array <uint128_union, 2> owords;
        void clear ();
        bool is_zero () const;
        std::string to_string () const;
        rai::uint256_t number () const;
    };
    using block_hash = uint256_union;
    using identifier = uint256_union;
    using address = uint256_union;
    using balance = uint256_union;
    using amount = uint256_union;
    using public_key = uint256_union;
    using private_key = uint256_union;
    using secret_key = uint256_union;
    using checksum = uint256_union;
    union uint512_union
    {
        uint512_union () = default;
        uint512_union (rai::uint512_t const &);
        bool operator == (rai::uint512_union const &) const;
        bool operator != (rai::uint512_union const &) const;
        rai::uint512_union & operator ^= (rai::uint512_union const &);
        void encode_hex (std::string &);
        bool decode_hex (std::string const &);
        rai::uint512_union salsa20_8 ();
        std::array <uint8_t, 64> bytes;
        std::array <uint32_t, 16> dwords;
        std::array <uint64_t, 8> qwords;
        std::array <uint256_union, 2> uint256s;
        void clear ();
        boost::multiprecision::uint512_t number ();
    };
    using signature = uint512_union;
    using endpoint = boost::asio::ip::udp::endpoint;
    using tcp_endpoint = boost::asio::ip::tcp::endpoint;
    bool parse_endpoint (std::string const &, rai::endpoint &);
    bool parse_tcp_endpoint (std::string const &, rai::tcp_endpoint &);
	bool reserved_address (rai::endpoint const &);
}

namespace std
{
    template <>
    struct hash <rai::uint256_union>
    {
        size_t operator () (rai::uint256_union const & data_a) const
        {
            return *reinterpret_cast <size_t const *> (data_a.bytes.data ());
        }
    };
    template <>
    struct hash <rai::uint256_t>
    {
        size_t operator () (rai::uint256_t const & number_a) const
        {
            return number_a.convert_to <size_t> ();
        }
    };
    template <size_t size>
    struct endpoint_hash
    {
    };
    template <>
    struct endpoint_hash <4>
    {
        size_t operator () (rai::endpoint const & endpoint_a) const
        {
            auto result (endpoint_a.address ().to_v4 ().to_ulong () ^ endpoint_a.port ());
            return result;
        }
    };
    template <>
    struct endpoint_hash <8>
    {
        size_t operator () (rai::endpoint const & endpoint_a) const
        {
            auto result ((endpoint_a.address ().to_v4 ().to_ulong () << 2) | endpoint_a.port ());
            return result;
        }
    };
    template <>
    struct hash <rai::endpoint>
    {
        size_t operator () (rai::endpoint const & endpoint_a) const
        {
            endpoint_hash <sizeof (size_t)> ehash;
            return ehash (endpoint_a);
        }
    };
}
namespace boost
{
    template <>
    struct hash <rai::endpoint>
    {
        size_t operator () (rai::endpoint const & endpoint_a) const
        {
            std::hash <rai::endpoint> hash;
            return hash (endpoint_a);
        }
    };
    template <>
    struct hash <rai::uint256_union>
    {
        size_t operator () (rai::uint256_union const & value_a) const
        {
            std::hash <rai::uint256_union> hash;
            return hash (value_a);
        }
    };
}

namespace rai {
    class block_visitor;
    enum class block_type : uint8_t
    {
        invalid,
        not_a_block,
        send,
        receive,
        open,
        change
    };
    class block
    {
    public:
        rai::uint256_union hash () const;
        virtual void hash (CryptoPP::SHA3 &) const = 0;
        virtual rai::block_hash previous () const = 0;
        virtual rai::block_hash source () const = 0;
        virtual void serialize (rai::stream &) const = 0;
        virtual void visit (rai::block_visitor &) const = 0;
        virtual bool operator == (rai::block const &) const = 0;
        virtual std::unique_ptr <rai::block> clone () const = 0;
        virtual rai::block_type type () const = 0;
    };
    std::unique_ptr <rai::block> deserialize_block (rai::stream &);
    void serialize_block (rai::stream &, rai::block const &);
    void sign_message (rai::private_key const &, rai::public_key const &, rai::uint256_union const &, rai::uint512_union &);
    bool validate_message (rai::public_key const &, rai::uint256_union const &, rai::uint512_union const &);
    class send_hashables
    {
    public:
        void hash (CryptoPP::SHA3 &) const;
        rai::address destination;
        rai::block_hash previous;
        rai::uint256_union balance;
    };
    class send_block : public rai::block
    {
    public:
        send_block () = default;
        send_block (send_block const &);
        using rai::block::hash;
        void hash (CryptoPP::SHA3 &) const override;
        rai::block_hash previous () const override;
        rai::block_hash source () const override;
        void serialize (rai::stream &) const override;
        bool deserialize (rai::stream &);
        void visit (rai::block_visitor &) const override;
        std::unique_ptr <rai::block> clone () const override;
        rai::block_type type () const override;
        bool operator == (rai::block const &) const override;
        bool operator == (rai::send_block const &) const;
        send_hashables hashables;
        rai::signature signature;
    };
    class receive_hashables
    {
    public:
        void hash (CryptoPP::SHA3 &) const;
        rai::block_hash previous;
        rai::block_hash source;
    };
    class receive_block : public rai::block
    {
    public:
        using rai::block::hash;
        void hash (CryptoPP::SHA3 &) const override;
        rai::block_hash previous () const override;
        rai::block_hash source () const override;
        void serialize (rai::stream &) const override;
        bool deserialize (rai::stream &);
        void visit (rai::block_visitor &) const override;
        std::unique_ptr <rai::block> clone () const override;
        rai::block_type type () const override;
        void sign (rai::private_key const &, rai::public_key const &, rai::uint256_union const &);
        bool validate (rai::public_key const &, rai::uint256_t const &) const;
        bool operator == (rai::block const &) const override;
        bool operator == (rai::receive_block const &) const;
        receive_hashables hashables;
        uint512_union signature;
    };
    class open_hashables
    {
    public:
        void hash (CryptoPP::SHA3 &) const;
        rai::address representative;
        rai::block_hash source;
    };
    class open_block : public rai::block
    {
    public:
        using rai::block::hash;
        void hash (CryptoPP::SHA3 &) const override;
        rai::block_hash previous () const override;
        rai::block_hash source () const override;
        void serialize (rai::stream &) const override;
        bool deserialize (rai::stream &);
        void visit (rai::block_visitor &) const override;
        std::unique_ptr <rai::block> clone () const override;
        rai::block_type type () const override;
        bool operator == (rai::block const &) const override;
        bool operator == (rai::open_block const &) const;
        rai::open_hashables hashables;
        rai::uint512_union signature;
    };
    class change_hashables
    {
    public:
        void hash (CryptoPP::SHA3 &) const;
        rai::address representative;
        rai::block_hash previous;
    };
    class change_block : public rai::block
    {
    public:
        using rai::block::hash;
        void hash (CryptoPP::SHA3 &) const override;
        rai::block_hash previous () const override;
        rai::block_hash source () const override;
        void serialize (rai::stream &) const override;
        bool deserialize (rai::stream &);
        void visit (rai::block_visitor &) const override;
        std::unique_ptr <rai::block> clone () const override;
        rai::block_type type () const override;
        bool operator == (rai::block const &) const override;
        bool operator == (rai::change_block const &) const;
        rai::change_hashables hashables;
        rai::uint512_union signature;
    };
    class block_visitor
    {
    public:
        virtual void send_block (rai::send_block const &) = 0;
        virtual void receive_block (rai::receive_block const &) = 0;
        virtual void open_block (rai::open_block const &) = 0;
        virtual void change_block (rai::change_block const &) = 0;
    };
    struct block_store_temp_t
    {
    };
    class frontier
    {
    public:
        void serialize (rai::stream &) const;
        bool deserialize (rai::stream &);
        bool operator == (rai::frontier const &) const;
        rai::uint256_union hash;
        rai::address representative;
        rai::uint256_union balance;
        uint64_t time;
    };
    class account_entry
    {
    public:
        account_entry * operator -> ();
        rai::address first;
        rai::frontier second;
    };
    class account_iterator
    {
    public:
        account_iterator (leveldb::DB &);
        account_iterator (leveldb::DB &, std::nullptr_t);
        account_iterator (leveldb::DB &, rai::address const &);
        account_iterator (rai::account_iterator &&) = default;
        account_iterator & operator ++ ();
        account_iterator & operator = (rai::account_iterator &&) = default;
        account_entry & operator -> ();
        bool operator == (rai::account_iterator const &) const;
        bool operator != (rai::account_iterator const &) const;
        void set_current ();
        std::unique_ptr <leveldb::Iterator> iterator;
        rai::account_entry current;
    };
    class block_entry
    {
    public:
        block_entry * operator -> ();
        rai::block_hash first;
        std::unique_ptr <rai::block> second;
    };
    class block_iterator
    {
    public:
        block_iterator (leveldb::DB &);
        block_iterator (leveldb::DB &, std::nullptr_t);
        block_iterator (rai::block_iterator &&) = default;
        block_iterator & operator ++ ();
        block_entry & operator -> ();
        bool operator == (rai::block_iterator const &) const;
        bool operator != (rai::block_iterator const &) const;
        void set_current ();
        std::unique_ptr <leveldb::Iterator> iterator;
        rai::block_entry current;
    };
    extern block_store_temp_t block_store_temp;
    class block_store
    {
    public:
        block_store (block_store_temp_t const &);
        block_store (boost::filesystem::path const &);
        
        uint64_t now ();
        
        rai::block_hash root (rai::block const &);
        void block_put (rai::block_hash const &, rai::block const &);
        std::unique_ptr <rai::block> block_get (rai::block_hash const &);
		void block_del (rai::block_hash const &);
        bool block_exists (rai::block_hash const &);
        block_iterator blocks_begin ();
        block_iterator blocks_end ();
        
        void latest_put (rai::address const &, rai::frontier const &);
        bool latest_get (rai::address const &, rai::frontier &);
		void latest_del (rai::address const &);
        bool latest_exists (rai::address const &);
        account_iterator latest_begin (rai::address const &);
        account_iterator latest_begin ();
        account_iterator latest_end ();
        
        void pending_put (rai::block_hash const &, rai::address const &, rai::uint256_union const &, rai::address const &);
        void pending_del (rai::identifier const &);
        bool pending_get (rai::identifier const &, rai::address &, rai::uint256_union &, rai::address &);
        bool pending_exists (rai::block_hash const &);
        
        rai::uint256_t representation_get (rai::address const &);
        void representation_put (rai::address const &, rai::uint256_t const &);
        
        void fork_put (rai::block_hash const &, rai::block const &);
        std::unique_ptr <rai::block> fork_get (rai::block_hash const &);
        
        void bootstrap_put (rai::block_hash const &, rai::block const &);
        std::unique_ptr <rai::block> bootstrap_get (rai::block_hash const &);
        void bootstrap_del (rai::block_hash const &);
        
        void checksum_put (uint64_t, uint8_t, rai::checksum const &);
        bool checksum_get (uint64_t, uint8_t, rai::checksum &);
        void checksum_del (uint64_t, uint8_t);
        
    private:
        // address -> block_hash, representative, balance, timestamp    // Address to frontier block, representative, balance, last_change
        std::unique_ptr <leveldb::DB> addresses;
        // block_hash -> block                                          // Mapping block hash to contents
        std::unique_ptr <leveldb::DB> blocks;
        // block_hash -> sender, amount, destination                    // Pending blocks to sender address, amount, destination address
        std::unique_ptr <leveldb::DB> pending;
        // address -> weight                                            // Representation
        std::unique_ptr <leveldb::DB> representation;
        // block_hash -> sequence, block                                // Previous block hash to most recent sequence and fork proof
        std::unique_ptr <leveldb::DB> forks;
        // block_hash -> block                                          // Unchecked bootstrap blocks
        std::unique_ptr <leveldb::DB> bootstrap;
        // block_hash -> block_hash                                     // Tracking successors for bootstrapping
        std::unique_ptr <leveldb::DB> successors;
        // (uint56_t, uint8_t) -> block_hash                            // Mapping of region to checksum
        std::unique_ptr <leveldb::DB> checksum;
    };
    enum class process_result
    {
        progress, // Hasn't been seen before, signed correctly
        bad_signature, // One or more signatures was bad, forged or transmission error
        old, // Already seen and was valid
        overspend, // Malicious attempt to overspend
        overreceive, // Malicious attempt to receive twice
        fork, // Malicious fork of existing block
        gap_previous, // Block marked as previous isn't in store
        gap_source, // Block marked as source isn't in store
        not_receive_from_send // Receive does not have a send source
    };
    class ledger
    {
    public:
        ledger (rai::block_store &);
		rai::address account (rai::block_hash const &);
		rai::uint256_t amount (rai::block_hash const &);
        rai::uint256_t balance (rai::block_hash const &);
		rai::uint256_t account_balance (rai::address const &);
        rai::uint256_t weight (rai::address const &);
		std::unique_ptr <rai::block> successor (rai::block_hash const &);
		rai::block_hash latest (rai::address const &);
        rai::address representative (rai::block_hash const &);
        rai::address representative_calculated (rai::block_hash const &);
        rai::address representative_cached (rai::block_hash const &);
        rai::uint256_t supply ();
        rai::process_result process (rai::block const &);
        void rollback (rai::block_hash const &);
        void change_latest (rai::address const &, rai::block_hash const &, rai::address const &, rai::uint256_union const &);
		void move_representation (rai::address const &, rai::address const &, rai::uint256_t const &);
        void checksum_update (rai::block_hash const &);
        rai::checksum checksum (rai::address const &, rai::address const &);
        rai::block_store & store;
    };
    class client;
    class vote
    {
    public:
        rai::uint256_union hash () const;
        rai::address address;
        rai::signature signature;
        uint64_t sequence;
        std::unique_ptr <rai::block> block;
    };
    class destructable
    {
    public:
        destructable (std::function <void ()>);
        ~destructable ();
        std::function <void ()> operation;
    };
    class votes : public std::enable_shared_from_this <rai::votes>
    {
    public:
        votes (std::shared_ptr <rai::client>, rai::block const &);
        void start ();
        void vote (rai::vote const &);
        void start_request (rai::block const &);
        void announce_vote ();
        void timeout_action (std::shared_ptr <rai::destructable>);
        std::pair <std::unique_ptr <rai::block>, rai::uint256_t> winner ();
        rai::uint256_t uncontested_threshold ();
        rai::uint256_t contested_threshold ();
        rai::uint256_t flip_threshold ();
        std::shared_ptr <rai::client> client;
        rai::block_hash const root;
		std::unique_ptr <rai::block> last_winner;
        uint64_t sequence;
        bool confirmed;
		std::chrono::system_clock::time_point last_vote;
        std::unordered_map <rai::address, std::pair <uint64_t, std::unique_ptr <rai::block>>> rep_votes;
    };
    class conflicts
    {
    public:
		conflicts (rai::client &);
        void start (rai::block const &, bool);
		void update (rai::vote const &);
        void stop (rai::block_hash const &);
        std::unordered_map <rai::block_hash, std::shared_ptr <rai::votes>> roots;
		rai::client & client;
        std::mutex mutex;
    };
    class keypair
    {
    public:
        keypair ();
        keypair (std::string const &);
        rai::public_key pub;
        rai::private_key prv;
    };
    enum class message_type : uint8_t
    {
        invalid,
        not_a_type,
        keepalive_req,
        keepalive_ack,
        publish_req,
        confirm_req,
        confirm_ack,
        confirm_unk,
        bulk_req,
		frontier_req
    };
    class message_visitor;
    class message
    {
    public:
        virtual ~message () = default;
        virtual void serialize (rai::stream &) = 0;
        virtual void visit (rai::message_visitor &) const = 0;
    };
    class keepalive_req : public message
    {
    public:
        void visit (rai::message_visitor &) const override;
        bool deserialize (rai::stream &);
        void serialize (rai::stream &) override;
		std::array <rai::endpoint, 24> peers;
    };
    class keepalive_ack : public message
    {
    public:
        void visit (rai::message_visitor &) const override;
        bool deserialize (rai::stream &);
        void serialize (rai::stream &) override;
		bool operator == (rai::keepalive_ack const &) const;
		std::array <rai::endpoint, 24> peers;
		rai::uint256_union checksum;
    };
    class publish_req : public message
    {
    public:
        publish_req () = default;
        publish_req (std::unique_ptr <rai::block>);
        void visit (rai::message_visitor &) const override;
        bool deserialize (rai::stream &);
        void serialize (rai::stream &) override;
        bool operator == (rai::publish_req const &) const;
        rai::uint256_union work;
        std::unique_ptr <rai::block> block;
    };
    class confirm_req : public message
    {
    public:
        bool deserialize (rai::stream &);
        void serialize (rai::stream &) override;
        void visit (rai::message_visitor &) const override;
        bool operator == (rai::confirm_req const &) const;
        rai::uint256_union work;
        std::unique_ptr <rai::block> block;
    };
    class confirm_ack : public message
    {
    public:
        bool deserialize (rai::stream &);
        void serialize (rai::stream &) override;
        void visit (rai::message_visitor &) const override;
        bool operator == (rai::confirm_ack const &) const;
        rai::vote vote;
        rai::uint256_union work;
    };
    class confirm_unk : public message
    {
    public:
        bool deserialize (rai::stream &);
        void serialize (rai::stream &) override;
        void visit (rai::message_visitor &) const override;
		rai::uint256_union hash () const;
        rai::address rep_hint;
    };
    class frontier_req : public message
    {
    public:
        bool deserialize (rai::stream &);
        void serialize (rai::stream &) override;
        void visit (rai::message_visitor &) const override;
        bool operator == (rai::frontier_req const &) const;
        rai::address start;
        uint32_t age;
        uint32_t count;
    };
    class bulk_req : public message
    {
    public:
        bool deserialize (rai::stream &);
        void serialize (rai::stream &) override;
        void visit (rai::message_visitor &) const override;
        rai::uint256_union start;
        rai::block_hash end;
        uint32_t count;
    };
    class message_visitor
    {
    public:
        virtual void keepalive_req (rai::keepalive_req const &) = 0;
        virtual void keepalive_ack (rai::keepalive_ack const &) = 0;
        virtual void publish_req (rai::publish_req const &) = 0;
        virtual void confirm_req (rai::confirm_req const &) = 0;
        virtual void confirm_ack (rai::confirm_ack const &) = 0;
        virtual void confirm_unk (rai::confirm_unk const &) = 0;
        virtual void bulk_req (rai::bulk_req const &) = 0;
        virtual void frontier_req (rai::frontier_req const &) = 0;
    };
    class key_entry
    {
    public:
        rai::key_entry * operator -> ();
        rai::public_key first;
        rai::private_key second;
    };
    class key_iterator
    {
    public:
        key_iterator (leveldb::DB *); // Begin iterator
        key_iterator (leveldb::DB *, std::nullptr_t); // End iterator
        key_iterator (leveldb::DB *, rai::uint256_union const &);
        key_iterator (rai::key_iterator &&) = default;
        void set_current ();
        key_iterator & operator ++ ();
        rai::key_entry & operator -> ();
        bool operator == (rai::key_iterator const &) const;
        bool operator != (rai::key_iterator const &) const;
        rai::key_entry current;
        std::unique_ptr <leveldb::Iterator> iterator;
    };
    class wallet
    {
    public:
        wallet (boost::filesystem::path const &);
        rai::uint256_union check ();
		bool rekey (rai::uint256_union const &);
        rai::uint256_union wallet_key ();
        void insert (rai::private_key const &);
        bool fetch (rai::public_key const &, rai::private_key &);
        bool generate_send (rai::ledger &, rai::public_key const &, rai::uint256_t const &, std::vector <std::unique_ptr <rai::send_block>> &);
		bool valid_password ();
        key_iterator find (rai::uint256_union const &);
        key_iterator begin ();
        key_iterator end ();
        rai::uint256_union hash_password (std::string const &);
        rai::uint256_union password;
    private:
        leveldb::DB * handle;
    };
    class operation
    {
    public:
        bool operator > (rai::operation const &) const;
        std::chrono::system_clock::time_point wakeup;
        std::function <void ()> function;
    };
    class processor_service
    {
    public:
        processor_service ();
        void run ();
        size_t poll ();
        size_t poll_one ();
        void add (std::chrono::system_clock::time_point const &, std::function <void ()> const &);
        void stop ();
        bool stopped ();
        size_t size ();
    private:
        bool done;
        std::mutex mutex;
        std::condition_variable condition;
        std::priority_queue <operation, std::vector <operation>, std::greater <operation>> operations;
    };
    class peer_information
    {
    public:
        rai::endpoint endpoint;
        std::chrono::system_clock::time_point last_contact;
        std::chrono::system_clock::time_point last_attempt;
    };
    class gap_information
    {
    public:
        std::chrono::system_clock::time_point arrival;
        rai::block_hash hash;
        std::unique_ptr <rai::block> block;
    };
    class gap_cache
    {
    public:
        gap_cache ();
        void add (rai::block const &, rai::block_hash);
        std::unique_ptr <rai::block> get (rai::block_hash const &);
        boost::multi_index_container
        <
            gap_information,
            boost::multi_index::indexed_by
            <
                boost::multi_index::hashed_unique <boost::multi_index::member <gap_information, rai::block_hash, &gap_information::hash>>,
                boost::multi_index::ordered_non_unique <boost::multi_index::member <gap_information, std::chrono::system_clock::time_point, &gap_information::arrival>>
            >
        > blocks;
        size_t const max;
    };
    using session = std::function <void (rai::confirm_ack const &, rai::endpoint const &)>;
    class processor
    {
    public:
        processor (rai::client &);
        void stop ();
        void find_network (std::vector <std::pair <std::string, std::string>> const &);
        void bootstrap (rai::tcp_endpoint const &, std::function <void ()> const &);
        rai::process_result process_receive (rai::block const &);
        void process_receive_republish (std::unique_ptr <rai::block>, rai::endpoint const &);
        void republish (std::unique_ptr <rai::block>, rai::endpoint const &);
		void process_message (rai::message &, rai::endpoint const &, bool);
		void process_unknown (rai::vectorstream &);
        void process_confirmation (rai::block const &, rai::endpoint const &);
        void process_confirmed (rai::block const &);
        void ongoing_keepalive ();
        rai::client & client;
        static std::chrono::seconds constexpr period = std::chrono::seconds (10);
        static std::chrono::seconds constexpr cutoff = period * 5;
    };
    class transactions
    {
    public:
        transactions (rai::ledger &, rai::wallet &, rai::processor &);
        bool receive (rai::send_block const &, rai::private_key const &, rai::address const &);
        bool send (rai::address const &, rai::uint256_t const &);
        void vote (rai::vote const &);
		bool rekey (rai::uint256_union const &);
        std::mutex mutex;
        rai::ledger & ledger;
        rai::wallet & wallet;
		rai::processor & processor;
    };
    class bootstrap_initiator : public std::enable_shared_from_this <bootstrap_initiator>
    {
    public:
        bootstrap_initiator (std::shared_ptr <rai::client>, std::function <void ()> const &);
        ~bootstrap_initiator ();
        void run (rai::tcp_endpoint const &);
        void connect_action (boost::system::error_code const &);
        void send_frontier_request ();
        void sent_request (boost::system::error_code const &, size_t);
        void run_receiver ();
        void finish_request ();
        void add_and_send (std::unique_ptr <rai::message>);
        void add_request (std::unique_ptr <rai::message>);
        std::queue <std::unique_ptr <rai::message>> requests;
        std::vector <uint8_t> send_buffer;
        std::shared_ptr <rai::client> client;
        boost::asio::ip::tcp::socket socket;
        std::function <void ()> complete_action;
        std::mutex mutex;
        static size_t const max_queue_size = 10;
    };
    class bulk_req_initiator : public std::enable_shared_from_this <bulk_req_initiator>
    {
    public:
        bulk_req_initiator (std::shared_ptr <rai::bootstrap_initiator> const &, std::unique_ptr <rai::bulk_req>);
        ~bulk_req_initiator ();
        void receive_block ();
        void received_type (boost::system::error_code const &, size_t);
        void received_block (boost::system::error_code const &, size_t);
        bool process_block (rai::block const &);
        bool process_end ();
        std::array <uint8_t, 4000> receive_buffer;
        std::unique_ptr <rai::bulk_req> request;
        rai::block_hash expecting;
        std::shared_ptr <rai::bootstrap_initiator> connection;
    };
    class frontier_req_initiator : public std::enable_shared_from_this <frontier_req_initiator>
    {
    public:
        frontier_req_initiator (std::shared_ptr <rai::bootstrap_initiator> const &, std::unique_ptr <rai::frontier_req>);
        ~frontier_req_initiator ();
        void receive_frontier ();
        void received_frontier (boost::system::error_code const &, size_t);
        std::array <uint8_t, 4000> receive_buffer;
        std::unique_ptr <rai::frontier_req> request;
        std::shared_ptr <rai::bootstrap_initiator> connection;
    };
    class work
    {
    public:
        work ();
        rai::uint256_union generate (rai::uint256_union const &, rai::uint256_union const &);
        rai::uint256_union create (rai::uint256_union const &);
        bool validate (rai::uint256_union const &, rai::uint256_union const &);
        rai::uint256_union threshold_requirement;
        size_t const entry_requirement;
        uint32_t const iteration_requirement;
        std::vector <rai::uint512_union> entries;
    };
    class network
    {
    public:
        network (boost::asio::io_service &, uint16_t, rai::client &);
        void receive ();
        void stop ();
        void receive_action (boost::system::error_code const &, size_t);
        void rpc_action (boost::system::error_code const &, size_t);
        void publish_block (rai::endpoint const &, std::unique_ptr <rai::block>);
        void confirm_block (std::unique_ptr <rai::block>, uint64_t);
        void merge_peers (std::shared_ptr <std::vector <uint8_t>> const &, std::array <rai::endpoint, 24> const &);
        void send_keepalive (rai::endpoint const &);
        void send_confirm_req (rai::endpoint const &, rai::block const &);
        void send_buffer (uint8_t const *, size_t, rai::endpoint const &, std::function <void (boost::system::error_code const &, size_t)>);
        void send_complete (boost::system::error_code const &, size_t);
        rai::endpoint endpoint ();
        rai::endpoint remote;
        std::array <uint8_t, 512> buffer;
        rai::work work;
        boost::asio::ip::udp::socket socket;
        boost::asio::io_service & service;
        rai::client & client;
        std::queue <std::tuple <uint8_t const *, size_t, rai::endpoint, std::function <void (boost::system::error_code const &, size_t)>>> sends;
        std::mutex mutex;
        uint64_t keepalive_req_count;
        uint64_t keepalive_ack_count;
        uint64_t publish_req_count;
        uint64_t confirm_req_count;
        uint64_t confirm_ack_count;
        uint64_t confirm_unk_count;
        uint64_t bad_sender_count;
        uint64_t unknown_count;
        uint64_t error_count;
        uint64_t insufficient_work_count;
        bool on;
    };
    class bootstrap_receiver
    {
    public:
        bootstrap_receiver (boost::asio::io_service &, uint16_t, rai::client &);
        void start ();
        void stop ();
        void accept_connection ();
        void accept_action (boost::system::error_code const &, std::shared_ptr <boost::asio::ip::tcp::socket>);
        rai::tcp_endpoint endpoint ();
        boost::asio::ip::tcp::acceptor acceptor;
        rai::tcp_endpoint local;
        boost::asio::io_service & service;
        rai::client & client;
        bool on;
    };
    class bootstrap_connection : public std::enable_shared_from_this <bootstrap_connection>
    {
    public:
        bootstrap_connection (std::shared_ptr <boost::asio::ip::tcp::socket>, std::shared_ptr <rai::client>);
        ~bootstrap_connection ();
        void receive ();
        void receive_type_action (boost::system::error_code const &, size_t);
        void receive_bulk_req_action (boost::system::error_code const &, size_t);
		void receive_frontier_req_action (boost::system::error_code const &, size_t);
		void add_request (std::unique_ptr <rai::message>);
		void finish_request ();
		void run_next ();
        std::array <uint8_t, 128> receive_buffer;
        std::shared_ptr <boost::asio::ip::tcp::socket> socket;
        std::shared_ptr <rai::client> client;
        std::mutex mutex;
        std::queue <std::unique_ptr <rai::message>> requests;
    };
    class bulk_req_response : public std::enable_shared_from_this <bulk_req_response>
    {
    public:
        bulk_req_response (std::shared_ptr <rai::bootstrap_connection> const &, std::unique_ptr <rai::bulk_req>);
        void set_current_end ();
        std::unique_ptr <rai::block> get_next ();
        void send_next ();
        void sent_action (boost::system::error_code const &, size_t);
        void send_finished ();
        void no_block_sent (boost::system::error_code const &, size_t);
        std::shared_ptr <rai::bootstrap_connection> connection;
        std::unique_ptr <rai::bulk_req> request;
        std::vector <uint8_t> send_buffer;
        rai::block_hash current;
    };
    class frontier_req_response : public std::enable_shared_from_this <frontier_req_response>
    {
    public:
        frontier_req_response (std::shared_ptr <rai::bootstrap_connection> const &, std::unique_ptr <rai::frontier_req>);
        void skip_old ();
		void send_next ();
        void sent_action (boost::system::error_code const &, size_t);
        void send_finished ();
        void no_block_sent (boost::system::error_code const &, size_t);
        std::pair <rai::uint256_union, rai::uint256_union> get_next ();
		account_iterator iterator;
        std::shared_ptr <rai::bootstrap_connection> connection;
        std::unique_ptr <rai::frontier_req> request;
        std::vector <uint8_t> send_buffer;
        size_t count;
    };
    class rpc
    {
    public:
		rpc (boost::shared_ptr <boost::asio::io_service>, boost::shared_ptr <boost::network::utils::thread_pool>, uint16_t, rai::client &, std::unordered_set <rai::uint256_union> const &);
        void start ();
        void stop ();
        boost::network::http::server <rai::rpc> server;
        void operator () (boost::network::http::server <rai::rpc>::request const &, boost::network::http::server <rai::rpc>::response &);
        void log (const char *) {}
        rai::client & client;
        bool on;
		std::unordered_set <rai::uint256_union> api_keys;
    };
    class peer_container
    {
    public:
		peer_container (rai::endpoint const &);
        bool known_peer (rai::endpoint const &);
        void incoming_from_peer (rai::endpoint const &);
		bool contacting_peer (rai::endpoint const &);
		void random_fill (std::array <rai::endpoint, 24> &);
        std::vector <peer_information> list ();
        void refresh_action ();
        void queue_next_refresh ();
        std::vector <rai::peer_information> purge_list (std::chrono::system_clock::time_point const &);
        size_t size ();
        bool empty ();
        std::mutex mutex;
		rai::endpoint self;
        boost::multi_index_container
        <peer_information,
            boost::multi_index::indexed_by
            <
                boost::multi_index::hashed_unique <boost::multi_index::member <peer_information, rai::endpoint, &peer_information::endpoint>>,
                boost::multi_index::ordered_non_unique <boost::multi_index::member <peer_information, std::chrono::system_clock::time_point, &peer_information::last_contact>>,
                boost::multi_index::ordered_non_unique <boost::multi_index::member <peer_information, std::chrono::system_clock::time_point, &peer_information::last_attempt>, std::greater <std::chrono::system_clock::time_point>>
            >
        > peers;
    };
    extern rai::keypair test_genesis_key;
    extern rai::address genesis_address;
    class genesis
    {
    public:
        explicit genesis ();
        void initialize (rai::block_store &) const;
        rai::block_hash hash () const;
        rai::send_block send1;
        rai::send_block send2;
        rai::open_block open;
    };
    class log
    {
    public:
        log ();
        void add (std::string const &);
        void dump_cerr ();
        boost::circular_buffer <std::pair <std::chrono::system_clock::time_point, std::string>> items;
    };
    class client : public std::enable_shared_from_this <rai::client>
    {
    public:
        client (boost::shared_ptr <boost::asio::io_service>, uint16_t, boost::filesystem::path const &, rai::processor_service &, rai::address const &);
        client (boost::shared_ptr <boost::asio::io_service>, uint16_t, rai::processor_service &, rai::address const &);
        ~client ();
        bool send (rai::public_key const &, rai::uint256_t const &);
        rai::uint256_t balance ();
        void start ();
        void stop ();
        std::shared_ptr <rai::client> shared ();
        bool is_representative ();
		void representative_vote (rai::votes &, rai::block const &);
        uint64_t scale_down (rai::uint256_t const &);
        rai::uint256_t scale_up (uint64_t);
        rai::log log;
        rai::address representative;
        rai::block_store store;
        rai::gap_cache gap_cache;
        rai::ledger ledger;
        rai::conflicts conflicts;
        rai::wallet wallet;
        rai::network network;
        rai::bootstrap_receiver bootstrap;
        rai::processor processor;
        rai::transactions transactions;
        rai::peer_container peers;
        rai::processor_service & service;
        rai::uint256_t scale;
    };
    class system
    {
    public:
        system (uint16_t, size_t);
        ~system ();
        void generate_activity (rai::client &);
        void generate_mass_activity (uint32_t, rai::client &);
        void generate_usage_traffic (uint32_t, uint32_t, size_t);
        void generate_usage_traffic (uint32_t, uint32_t);
        rai::uint256_t get_random_amount (rai::client &);
        void generate_send_new (rai::client &);
        void generate_send_existing (rai::client &);
        boost::shared_ptr <boost::asio::io_service> service;
        rai::processor_service processor;
        std::vector <std::shared_ptr <rai::client>> clients;
    };
}