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
namespace mu_coin {
    using stream = std::basic_streambuf <uint8_t>;
    using bufferstream = boost::iostreams::stream_buffer <boost::iostreams::basic_array_source <uint8_t>>;
    using vectorstream = boost::iostreams::stream_buffer <boost::iostreams::back_insert_device <std::vector <uint8_t>>>;
    template <typename T>
    bool read (mu_coin::stream & stream_a, T & value)
    {
        auto amount_read (stream_a.sgetn (reinterpret_cast <uint8_t *> (&value), sizeof (value)));
        return amount_read != sizeof (value);
    }
    template <typename T>
    void write (mu_coin::stream & stream_a, T const & value)
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
        uint128_union (mu_coin::uint128_union const &) = default;
        uint128_union (mu_coin::uint128_t const &);
        std::array <uint8_t, 16> bytes;
        std::array <uint64_t, 2> qwords;
    };
    union uint256_union
    {
        uint256_union () = default;
        uint256_union (uint64_t);
        uint256_union (mu_coin::uint256_t const &);
        uint256_union (std::string const &);
        uint256_union (mu_coin::uint256_union const &, mu_coin::uint256_union const &, uint128_union const &);
        uint256_union prv (uint256_union const &, uint128_union const &) const;
        uint256_union & operator = (leveldb::Slice const &);
        uint256_union & operator ^= (mu_coin::uint256_union const &);
        uint256_union operator ^ (mu_coin::uint256_union const &) const;
        bool operator == (mu_coin::uint256_union const &) const;
        bool operator != (mu_coin::uint256_union const &) const;
        bool operator < (mu_coin::uint256_union const &) const;
        void encode_hex (std::string &) const;
        bool decode_hex (std::string const &);
        void encode_dec (std::string &) const;
        bool decode_dec (std::string const &);
        void encode_base58check (std::string &) const;
        bool decode_base58check (std::string const &);
        void serialize (mu_coin::stream &) const;
        bool deserialize (mu_coin::stream &);
        std::array <uint8_t, 32> bytes;
        std::array <char, 32> chars;
        std::array <uint64_t, 4> qwords;
        std::array <uint128_union, 2> owords;
        void clear ();
        bool is_zero () const;
        std::string to_string () const;
        mu_coin::uint256_t number () const;
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
        uint512_union (mu_coin::uint512_t const &);
        bool operator == (mu_coin::uint512_union const &) const;
        void encode_hex (std::string &);
        bool decode_hex (std::string const &);
        std::array <uint8_t, 64> bytes;
        std::array <uint64_t, 8> qwords;
        std::array <uint256_union, 2> uint256s;
        void clear ();
        boost::multiprecision::uint512_t number ();
    };
    using signature = uint512_union;
    using endpoint = boost::asio::ip::udp::endpoint;
    using tcp_endpoint = boost::asio::ip::tcp::endpoint;
    bool parse_endpoint (std::string const &, mu_coin::endpoint &);
    bool parse_tcp_endpoint (std::string const &, mu_coin::tcp_endpoint &);
	bool reserved_address (mu_coin::endpoint const &);
}

namespace std
{
    template <>
    struct hash <mu_coin::uint256_union>
    {
        size_t operator () (mu_coin::uint256_union const & data_a) const
        {
            return *reinterpret_cast <size_t const *> (data_a.bytes.data ());
        }
    };
    template <>
    struct hash <mu_coin::uint256_t>
    {
        size_t operator () (mu_coin::uint256_t const & number_a) const
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
        size_t operator () (mu_coin::endpoint const & endpoint_a) const
        {
            auto result (endpoint_a.address ().to_v4 ().to_ulong () ^ endpoint_a.port ());
            return result;
        }
    };
    template <>
    struct endpoint_hash <8>
    {
        size_t operator () (mu_coin::endpoint const & endpoint_a) const
        {
            auto result ((endpoint_a.address ().to_v4 ().to_ulong () << 2) | endpoint_a.port ());
            return result;
        }
    };
    template <>
    struct hash <mu_coin::endpoint>
    {
        size_t operator () (mu_coin::endpoint const & endpoint_a) const
        {
            endpoint_hash <sizeof (size_t)> ehash;
            return ehash (endpoint_a);
        }
    };
}
namespace boost
{
    template <>
    struct hash <mu_coin::endpoint>
    {
        size_t operator () (mu_coin::endpoint const & endpoint_a) const
        {
            std::hash <mu_coin::endpoint> hash;
            return hash (endpoint_a);
        }
    };
    template <>
    struct hash <mu_coin::uint256_union>
    {
        size_t operator () (mu_coin::uint256_union const & value_a) const
        {
            std::hash <mu_coin::uint256_union> hash;
            return hash (value_a);
        }
    };
}

namespace mu_coin {
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
        mu_coin::uint256_union hash () const;
        virtual void hash (CryptoPP::SHA3 &) const = 0;
        virtual mu_coin::block_hash previous () const = 0;
        virtual mu_coin::block_hash source () const = 0;
        virtual void serialize (mu_coin::stream &) const = 0;
        virtual void visit (mu_coin::block_visitor &) const = 0;
        virtual bool operator == (mu_coin::block const &) const = 0;
        virtual std::unique_ptr <mu_coin::block> clone () const = 0;
        virtual mu_coin::block_type type () const = 0;
    };
    std::unique_ptr <mu_coin::block> deserialize_block (mu_coin::stream &);
    void serialize_block (mu_coin::stream &, mu_coin::block const &);
    void sign_message (mu_coin::private_key const &, mu_coin::public_key const &, mu_coin::uint256_union const &, mu_coin::uint512_union &);
    bool validate_message (mu_coin::public_key const &, mu_coin::uint256_union const &, mu_coin::uint512_union const &);
    class send_hashables
    {
    public:
        void hash (CryptoPP::SHA3 &) const;
        mu_coin::address destination;
        mu_coin::block_hash previous;
        mu_coin::uint256_union balance;
    };
    class send_block : public mu_coin::block
    {
    public:
        send_block () = default;
        send_block (send_block const &);
        using mu_coin::block::hash;
        void hash (CryptoPP::SHA3 &) const override;
        mu_coin::block_hash previous () const override;
        mu_coin::block_hash source () const override;
        void serialize (mu_coin::stream &) const override;
        bool deserialize (mu_coin::stream &);
        void visit (mu_coin::block_visitor &) const override;
        std::unique_ptr <mu_coin::block> clone () const override;
        mu_coin::block_type type () const override;
        bool operator == (mu_coin::block const &) const override;
        bool operator == (mu_coin::send_block const &) const;
        send_hashables hashables;
        mu_coin::signature signature;
    };
    class receive_hashables
    {
    public:
        void hash (CryptoPP::SHA3 &) const;
        mu_coin::block_hash previous;
        mu_coin::block_hash source;
    };
    class receive_block : public mu_coin::block
    {
    public:
        using mu_coin::block::hash;
        void hash (CryptoPP::SHA3 &) const override;
        mu_coin::block_hash previous () const override;
        mu_coin::block_hash source () const override;
        void serialize (mu_coin::stream &) const override;
        bool deserialize (mu_coin::stream &);
        void visit (mu_coin::block_visitor &) const override;
        std::unique_ptr <mu_coin::block> clone () const override;
        mu_coin::block_type type () const override;
        void sign (mu_coin::private_key const &, mu_coin::public_key const &, mu_coin::uint256_union const &);
        bool validate (mu_coin::public_key const &, mu_coin::uint256_t const &) const;
        bool operator == (mu_coin::block const &) const override;
        bool operator == (mu_coin::receive_block const &) const;
        receive_hashables hashables;
        uint512_union signature;
    };
    class open_hashables
    {
    public:
        void hash (CryptoPP::SHA3 &) const;
        mu_coin::address representative;
        mu_coin::block_hash source;
    };
    class open_block : public mu_coin::block
    {
    public:
        using mu_coin::block::hash;
        void hash (CryptoPP::SHA3 &) const override;
        mu_coin::block_hash previous () const override;
        mu_coin::block_hash source () const override;
        void serialize (mu_coin::stream &) const override;
        bool deserialize (mu_coin::stream &);
        void visit (mu_coin::block_visitor &) const override;
        std::unique_ptr <mu_coin::block> clone () const override;
        mu_coin::block_type type () const override;
        bool operator == (mu_coin::block const &) const override;
        bool operator == (mu_coin::open_block const &) const;
        mu_coin::open_hashables hashables;
        mu_coin::uint512_union signature;
    };
    class change_hashables
    {
    public:
        void hash (CryptoPP::SHA3 &) const;
        mu_coin::address representative;
        mu_coin::block_hash previous;
    };
    class change_block : public mu_coin::block
    {
    public:
        using mu_coin::block::hash;
        void hash (CryptoPP::SHA3 &) const override;
        mu_coin::block_hash previous () const override;
        mu_coin::block_hash source () const override;
        void serialize (mu_coin::stream &) const override;
        bool deserialize (mu_coin::stream &);
        void visit (mu_coin::block_visitor &) const override;
        std::unique_ptr <mu_coin::block> clone () const override;
        mu_coin::block_type type () const override;
        bool operator == (mu_coin::block const &) const override;
        bool operator == (mu_coin::change_block const &) const;
        mu_coin::change_hashables hashables;
        mu_coin::uint512_union signature;
    };
    class block_visitor
    {
    public:
        virtual void send_block (mu_coin::send_block const &) = 0;
        virtual void receive_block (mu_coin::receive_block const &) = 0;
        virtual void open_block (mu_coin::open_block const &) = 0;
        virtual void change_block (mu_coin::change_block const &) = 0;
    };
    struct block_store_temp_t
    {
    };
    class frontier
    {
    public:
        void serialize (mu_coin::stream &) const;
        bool deserialize (mu_coin::stream &);
        bool operator == (mu_coin::frontier const &) const;
        mu_coin::uint256_union hash;
        mu_coin::address representative;
        mu_coin::uint256_union balance;
        uint64_t time;
    };
    class account_entry
    {
    public:
        account_entry * operator -> ();
        mu_coin::address first;
        mu_coin::frontier second;
    };
    class account_iterator
    {
    public:
        account_iterator (leveldb::DB &);
        account_iterator (leveldb::DB &, std::nullptr_t);
        account_iterator (leveldb::DB &, mu_coin::address const &);
        account_iterator (mu_coin::account_iterator &&) = default;
        account_iterator & operator ++ ();
        account_iterator & operator = (mu_coin::account_iterator &&) = default;
        account_entry & operator -> ();
        bool operator == (mu_coin::account_iterator const &) const;
        bool operator != (mu_coin::account_iterator const &) const;
        void set_current ();
        std::unique_ptr <leveldb::Iterator> iterator;
        mu_coin::account_entry current;
    };
    class block_entry
    {
    public:
        block_entry * operator -> ();
        mu_coin::block_hash first;
        std::unique_ptr <mu_coin::block> second;
    };
    class block_iterator
    {
    public:
        block_iterator (leveldb::DB &);
        block_iterator (leveldb::DB &, std::nullptr_t);
        block_iterator (mu_coin::block_iterator &&) = default;
        block_iterator & operator ++ ();
        block_entry & operator -> ();
        bool operator == (mu_coin::block_iterator const &) const;
        bool operator != (mu_coin::block_iterator const &) const;
        void set_current ();
        std::unique_ptr <leveldb::Iterator> iterator;
        mu_coin::block_entry current;
    };
    extern block_store_temp_t block_store_temp;
    class block_store
    {
    public:
        block_store (block_store_temp_t const &);
        block_store (boost::filesystem::path const &);
        
        uint64_t now ();
        
        mu_coin::block_hash root (mu_coin::block const &);
        void block_put (mu_coin::block_hash const &, mu_coin::block const &);
        std::unique_ptr <mu_coin::block> block_get (mu_coin::block_hash const &);
		void block_del (mu_coin::block_hash const &);
        bool block_exists (mu_coin::block_hash const &);
        block_iterator blocks_begin ();
        block_iterator blocks_end ();
        
        void latest_put (mu_coin::address const &, mu_coin::frontier const &);
        bool latest_get (mu_coin::address const &, mu_coin::frontier &);
		void latest_del (mu_coin::address const &);
        bool latest_exists (mu_coin::address const &);
        account_iterator latest_begin (mu_coin::address const &);
        account_iterator latest_begin ();
        account_iterator latest_end ();
        
        void pending_put (mu_coin::block_hash const &, mu_coin::address const &, mu_coin::uint256_union const &, mu_coin::address const &);
        void pending_del (mu_coin::identifier const &);
        bool pending_get (mu_coin::identifier const &, mu_coin::address &, mu_coin::uint256_union &, mu_coin::address &);
        bool pending_exists (mu_coin::block_hash const &);
        
        mu_coin::uint256_t representation_get (mu_coin::address const &);
        void representation_put (mu_coin::address const &, mu_coin::uint256_t const &);
        
        void fork_put (mu_coin::block_hash const &, mu_coin::block const &);
        std::unique_ptr <mu_coin::block> fork_get (mu_coin::block_hash const &);
        
        void bootstrap_put (mu_coin::block_hash const &, mu_coin::block const &);
        std::unique_ptr <mu_coin::block> bootstrap_get (mu_coin::block_hash const &);
        void bootstrap_del (mu_coin::block_hash const &);
        
        void checksum_put (uint64_t, uint8_t, mu_coin::checksum const &);
        bool checksum_get (uint64_t, uint8_t, mu_coin::checksum &);
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
        ledger (mu_coin::block_store &);
		mu_coin::address account (mu_coin::block_hash const &);
		mu_coin::uint256_t amount (mu_coin::block_hash const &);
        mu_coin::uint256_t balance (mu_coin::block_hash const &);
		mu_coin::uint256_t account_balance (mu_coin::address const &);
        mu_coin::uint256_t weight (mu_coin::address const &);
		std::unique_ptr <mu_coin::block> successor (mu_coin::block_hash const &);
		mu_coin::block_hash latest (mu_coin::address const &);
        mu_coin::address representative (mu_coin::block_hash const &);
        mu_coin::address representative_calculated (mu_coin::block_hash const &);
        mu_coin::address representative_cached (mu_coin::block_hash const &);
        mu_coin::uint256_t supply ();
        mu_coin::process_result process (mu_coin::block const &);
        void rollback (mu_coin::block_hash const &);
        void change_latest (mu_coin::address const &, mu_coin::block_hash const &, mu_coin::address const &, mu_coin::uint256_union const &);
		void move_representation (mu_coin::address const &, mu_coin::address const &, mu_coin::uint256_t const &);
        void checksum_update (mu_coin::block_hash const &);
        mu_coin::checksum checksum (mu_coin::address const &, mu_coin::address const &);
        mu_coin::block_store & store;
    };
    class client;
    class vote
    {
    public:
        mu_coin::uint256_union hash () const;
        mu_coin::address address;
        mu_coin::signature signature;
        uint64_t sequence;
        std::unique_ptr <mu_coin::block> block;
    };
    class destructable
    {
    public:
        destructable (std::function <void ()>);
        ~destructable ();
        std::function <void ()> operation;
    };
    class votes : public std::enable_shared_from_this <mu_coin::votes>
    {
    public:
        votes (std::shared_ptr <mu_coin::client>, mu_coin::block const &);
        void start ();
        void vote (mu_coin::vote const &);
        void start_request (mu_coin::block const &);
        void announce_vote ();
        void timeout_action (std::shared_ptr <mu_coin::destructable>);
        std::pair <std::unique_ptr <mu_coin::block>, mu_coin::uint256_t> winner ();
        mu_coin::uint256_t uncontested_threshold ();
        mu_coin::uint256_t contested_threshold ();
        mu_coin::uint256_t flip_threshold ();
        std::shared_ptr <mu_coin::client> client;
        mu_coin::block_hash const root;
		std::unique_ptr <mu_coin::block> last_winner;
        uint64_t sequence;
        bool confirmed;
		std::chrono::system_clock::time_point last_vote;
        std::unordered_map <mu_coin::address, std::pair <uint64_t, std::unique_ptr <mu_coin::block>>> rep_votes;
    };
    class conflicts
    {
    public:
		conflicts (mu_coin::client &);
        void start (mu_coin::block const &, bool);
		void update (mu_coin::vote const &);
        void stop (mu_coin::block_hash const &);
        std::unordered_map <mu_coin::block_hash, std::shared_ptr <mu_coin::votes>> roots;
		mu_coin::client & client;
        std::mutex mutex;
    };
    class keypair
    {
    public:
        keypair ();
        keypair (std::string const &);
        mu_coin::public_key pub;
        mu_coin::private_key prv;
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
        virtual void serialize (mu_coin::stream &) = 0;
        virtual void visit (mu_coin::message_visitor &) const = 0;
    };
    class keepalive_req : public message
    {
    public:
        void visit (mu_coin::message_visitor &) const override;
        bool deserialize (mu_coin::stream &);
        void serialize (mu_coin::stream &) override;
		std::array <mu_coin::endpoint, 24> peers;
    };
    class keepalive_ack : public message
    {
    public:
        void visit (mu_coin::message_visitor &) const override;
        bool deserialize (mu_coin::stream &);
        void serialize (mu_coin::stream &) override;
		bool operator == (mu_coin::keepalive_ack const &) const;
		std::array <mu_coin::endpoint, 24> peers;
		mu_coin::uint256_union checksum;
    };
    class publish_req : public message
    {
    public:
        publish_req () = default;
        publish_req (std::unique_ptr <mu_coin::block>);
        void visit (mu_coin::message_visitor &) const override;
        bool deserialize (mu_coin::stream &);
        void serialize (mu_coin::stream &) override;
        std::unique_ptr <mu_coin::block> block;
    };
    class confirm_req : public message
    {
    public:
        bool deserialize (mu_coin::stream &);
        void serialize (mu_coin::stream &) override;
        void visit (mu_coin::message_visitor &) const override;
        bool operator == (mu_coin::confirm_req const &) const;
        std::unique_ptr <mu_coin::block> block;
    };
    class confirm_ack : public message
    {
    public:
        bool deserialize (mu_coin::stream &);
        void serialize (mu_coin::stream &) override;
        void visit (mu_coin::message_visitor &) const override;
        bool operator == (mu_coin::confirm_ack const &) const;
        mu_coin::vote vote;
    };
    class confirm_unk : public message
    {
    public:
        bool deserialize (mu_coin::stream &);
        void serialize (mu_coin::stream &) override;
        void visit (mu_coin::message_visitor &) const override;
		mu_coin::uint256_union hash () const;
        mu_coin::address rep_hint;
    };
    class frontier_req : public message
    {
    public:
        bool deserialize (mu_coin::stream &);
        void serialize (mu_coin::stream &) override;
        void visit (mu_coin::message_visitor &) const override;
        bool operator == (mu_coin::frontier_req const &) const;
        mu_coin::address start;
        uint32_t age;
        uint32_t count;
    };
    class bulk_req : public message
    {
    public:
        bool deserialize (mu_coin::stream &);
        void serialize (mu_coin::stream &) override;
        void visit (mu_coin::message_visitor &) const override;
        mu_coin::uint256_union start;
        mu_coin::block_hash end;
        uint32_t count;
    };
    class message_visitor
    {
    public:
        virtual void keepalive_req (mu_coin::keepalive_req const &) = 0;
        virtual void keepalive_ack (mu_coin::keepalive_ack const &) = 0;
        virtual void publish_req (mu_coin::publish_req const &) = 0;
        virtual void confirm_req (mu_coin::confirm_req const &) = 0;
        virtual void confirm_ack (mu_coin::confirm_ack const &) = 0;
        virtual void confirm_unk (mu_coin::confirm_unk const &) = 0;
        virtual void bulk_req (mu_coin::bulk_req const &) = 0;
        virtual void frontier_req (mu_coin::frontier_req const &) = 0;
    };
    class key_entry
    {
    public:
        mu_coin::key_entry * operator -> ();
        mu_coin::public_key first;
        mu_coin::private_key second;
    };
    class key_iterator
    {
    public:
        key_iterator (leveldb::DB *); // Begin iterator
        key_iterator (leveldb::DB *, std::nullptr_t); // End iterator
        key_iterator (leveldb::DB *, mu_coin::uint256_union const &);
        key_iterator (mu_coin::key_iterator &&) = default;
        void set_current ();
        key_iterator & operator ++ ();
        mu_coin::key_entry & operator -> ();
        bool operator == (mu_coin::key_iterator const &) const;
        bool operator != (mu_coin::key_iterator const &) const;
        mu_coin::key_entry current;
        std::unique_ptr <leveldb::Iterator> iterator;
    };
    class wallet
    {
    public:
        wallet (mu_coin::uint256_union const &, boost::filesystem::path const &);
        mu_coin::uint256_union check ();
		void rekey (mu_coin::uint256_union const &);
        mu_coin::uint256_union wallet_key ();
        void insert (mu_coin::private_key const &);
        bool fetch (mu_coin::public_key const &, mu_coin::private_key &);
        bool generate_send (mu_coin::ledger &, mu_coin::public_key const &, mu_coin::uint256_t const &, std::vector <std::unique_ptr <mu_coin::send_block>> &);
		bool valid_password ();
        key_iterator find (mu_coin::uint256_union const &);
        key_iterator begin ();
        key_iterator end ();
        mu_coin::uint256_union password;
    private:
        leveldb::DB * handle;
    };
    class operation
    {
    public:
        bool operator > (mu_coin::operation const &) const;
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
        mu_coin::endpoint endpoint;
        std::chrono::system_clock::time_point last_contact;
        std::chrono::system_clock::time_point last_attempt;
    };
    class gap_information
    {
    public:
        std::chrono::system_clock::time_point arrival;
        mu_coin::block_hash hash;
        std::unique_ptr <mu_coin::block> block;
    };
    class gap_cache
    {
    public:
        gap_cache ();
        void add (mu_coin::block const &, mu_coin::block_hash);
        std::unique_ptr <mu_coin::block> get (mu_coin::block_hash const &);
        boost::multi_index_container
        <
            gap_information,
            boost::multi_index::indexed_by
            <
                boost::multi_index::hashed_unique <boost::multi_index::member <gap_information, mu_coin::block_hash, &gap_information::hash>>,
                boost::multi_index::ordered_non_unique <boost::multi_index::member <gap_information, std::chrono::system_clock::time_point, &gap_information::arrival>>
            >
        > blocks;
        size_t const max;
    };
    using session = std::function <void (mu_coin::confirm_ack const &, mu_coin::endpoint const &)>;
    class processor
    {
    public:
        processor (mu_coin::client &);
        void stop ();
        void bootstrap (mu_coin::tcp_endpoint const &, std::function <void ()> const &);
        mu_coin::process_result process_receive (mu_coin::block const &);
        void process_receive_republish (std::unique_ptr <mu_coin::block>, mu_coin::endpoint const &);
        void republish (std::unique_ptr <mu_coin::block>, mu_coin::endpoint const &);
		void process_message (mu_coin::message &, mu_coin::endpoint const &, bool);
		void process_unknown (mu_coin::vectorstream &);
        void process_confirmation (mu_coin::block const &, mu_coin::endpoint const &);
        void process_confirmed (mu_coin::block const &);
        void ongoing_keepalive ();
        mu_coin::client & client;
        static std::chrono::seconds constexpr period = std::chrono::seconds (10);
        static std::chrono::seconds constexpr cutoff = period * 5;
    };
    class transactions
    {
    public:
        transactions (mu_coin::ledger &, mu_coin::wallet &, mu_coin::processor &);
        bool receive (mu_coin::send_block const &, mu_coin::private_key const &, mu_coin::address const &);
        bool send (mu_coin::address const &, mu_coin::uint256_t const &);
        void vote (mu_coin::vote const &);
		void rekey (mu_coin::uint256_union const &);
        std::mutex mutex;
        mu_coin::ledger & ledger;
        mu_coin::wallet & wallet;
		mu_coin::processor & processor;
    };
    class bootstrap_initiator : public std::enable_shared_from_this <bootstrap_initiator>
    {
    public:
        bootstrap_initiator (std::shared_ptr <mu_coin::client>, std::function <void ()> const &);
        ~bootstrap_initiator ();
        void run (mu_coin::tcp_endpoint const &);
        void connect_action (boost::system::error_code const &);
        void send_frontier_request ();
        void sent_request (boost::system::error_code const &, size_t);
        void run_receiver ();
        void finish_request ();
        void add_and_send (std::unique_ptr <mu_coin::message>);
        void add_request (std::unique_ptr <mu_coin::message>);
        std::queue <std::unique_ptr <mu_coin::message>> requests;
        std::vector <uint8_t> send_buffer;
        std::shared_ptr <mu_coin::client> client;
        boost::asio::ip::tcp::socket socket;
        std::function <void ()> complete_action;
        std::mutex mutex;
        static size_t const max_queue_size = 10;
    };
    class bulk_req_initiator : public std::enable_shared_from_this <bulk_req_initiator>
    {
    public:
        bulk_req_initiator (std::shared_ptr <mu_coin::bootstrap_initiator> const &, std::unique_ptr <mu_coin::bulk_req>);
        ~bulk_req_initiator ();
        void receive_block ();
        void received_type (boost::system::error_code const &, size_t);
        void received_block (boost::system::error_code const &, size_t);
        bool process_block (mu_coin::block const &);
        bool process_end ();
        std::array <uint8_t, 4000> receive_buffer;
        std::unique_ptr <mu_coin::bulk_req> request;
        mu_coin::block_hash expecting;
        std::shared_ptr <mu_coin::bootstrap_initiator> connection;
    };
    class frontier_req_initiator : public std::enable_shared_from_this <frontier_req_initiator>
    {
    public:
        frontier_req_initiator (std::shared_ptr <mu_coin::bootstrap_initiator> const &, std::unique_ptr <mu_coin::frontier_req>);
        ~frontier_req_initiator ();
        void receive_frontier ();
        void received_frontier (boost::system::error_code const &, size_t);
        std::array <uint8_t, 4000> receive_buffer;
        std::unique_ptr <mu_coin::frontier_req> request;
        std::shared_ptr <mu_coin::bootstrap_initiator> connection;
    };
    class network
    {
    public:
        network (boost::asio::io_service &, uint16_t, mu_coin::client &);
        void receive ();
        void stop ();
        void receive_action (boost::system::error_code const &, size_t);
        void rpc_action (boost::system::error_code const &, size_t);
        void publish_block (mu_coin::endpoint const &, std::unique_ptr <mu_coin::block>);
        void confirm_block (std::unique_ptr <mu_coin::block>, uint64_t);
        void merge_peers (std::shared_ptr <std::vector <uint8_t>> const &, std::array <mu_coin::endpoint, 24> const &);
        void send_keepalive (mu_coin::endpoint const &);
        void send_confirm_req (mu_coin::endpoint const &, mu_coin::block const &);
        void send_buffer (uint8_t const *, size_t, mu_coin::endpoint const &, std::function <void (boost::system::error_code const &, size_t)>);
        void send_complete (boost::system::error_code const &, size_t);
        mu_coin::endpoint endpoint ();
        mu_coin::endpoint remote;
        std::array <uint8_t, 512> buffer;
        boost::asio::ip::udp::socket socket;
        boost::asio::io_service & service;
        mu_coin::client & client;
        std::queue <std::tuple <uint8_t const *, size_t, mu_coin::endpoint, std::function <void (boost::system::error_code const &, size_t)>>> sends;
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
        bool on;
    };
    class bootstrap_receiver
    {
    public:
        bootstrap_receiver (boost::asio::io_service &, uint16_t, mu_coin::client &);
        void start ();
        void stop ();
        void accept_connection ();
        void accept_action (boost::system::error_code const &, std::shared_ptr <boost::asio::ip::tcp::socket>);
        mu_coin::tcp_endpoint endpoint ();
        boost::asio::ip::tcp::acceptor acceptor;
        mu_coin::tcp_endpoint local;
        boost::asio::io_service & service;
        mu_coin::client & client;
        bool on;
    };
    class bootstrap_connection : public std::enable_shared_from_this <bootstrap_connection>
    {
    public:
        bootstrap_connection (std::shared_ptr <boost::asio::ip::tcp::socket>, std::shared_ptr <mu_coin::client>);
        ~bootstrap_connection ();
        void receive ();
        void receive_type_action (boost::system::error_code const &, size_t);
        void receive_bulk_req_action (boost::system::error_code const &, size_t);
		void receive_frontier_req_action (boost::system::error_code const &, size_t);
		void add_request (std::unique_ptr <mu_coin::message>);
		void finish_request ();
		void run_next ();
        std::array <uint8_t, 128> receive_buffer;
        std::shared_ptr <boost::asio::ip::tcp::socket> socket;
        std::shared_ptr <mu_coin::client> client;
        std::mutex mutex;
        std::queue <std::unique_ptr <mu_coin::message>> requests;
    };
    class bulk_req_response : public std::enable_shared_from_this <bulk_req_response>
    {
    public:
        bulk_req_response (std::shared_ptr <mu_coin::bootstrap_connection> const &, std::unique_ptr <mu_coin::bulk_req>);
        void set_current_end ();
        std::unique_ptr <mu_coin::block> get_next ();
        void send_next ();
        void sent_action (boost::system::error_code const &, size_t);
        void send_finished ();
        void no_block_sent (boost::system::error_code const &, size_t);
        std::shared_ptr <mu_coin::bootstrap_connection> connection;
        std::unique_ptr <mu_coin::bulk_req> request;
        std::vector <uint8_t> send_buffer;
        mu_coin::block_hash current;
    };
    class frontier_req_response : public std::enable_shared_from_this <frontier_req_response>
    {
    public:
        frontier_req_response (std::shared_ptr <mu_coin::bootstrap_connection> const &, std::unique_ptr <mu_coin::frontier_req>);
        void skip_old ();
		void send_next ();
        void sent_action (boost::system::error_code const &, size_t);
        void send_finished ();
        void no_block_sent (boost::system::error_code const &, size_t);
        std::pair <mu_coin::uint256_union, mu_coin::uint256_union> get_next ();
		account_iterator iterator;
        std::shared_ptr <mu_coin::bootstrap_connection> connection;
        std::unique_ptr <mu_coin::frontier_req> request;
        std::vector <uint8_t> send_buffer;
        size_t count;
    };
    class rpc
    {
    public:
        rpc (boost::shared_ptr <boost::asio::io_service>, boost::shared_ptr <boost::network::utils::thread_pool>, uint16_t, mu_coin::client &);
        void start ();
        void stop ();
        boost::network::http::server <mu_coin::rpc> server;
        void operator () (boost::network::http::server <mu_coin::rpc>::request const &, boost::network::http::server <mu_coin::rpc>::response &);
        void log (const char *) {}
        mu_coin::client & client;
        bool on;
    };
    class peer_container
    {
    public:
		peer_container (mu_coin::endpoint const &);
        bool known_peer (mu_coin::endpoint const &);
        void incoming_from_peer (mu_coin::endpoint const &);
		bool contacting_peer (mu_coin::endpoint const &);
		void random_fill (std::array <mu_coin::endpoint, 24> &);
        std::vector <peer_information> list ();
        void refresh_action ();
        void queue_next_refresh ();
        std::vector <mu_coin::peer_information> purge_list (std::chrono::system_clock::time_point const &);
        size_t size ();
        bool empty ();
        std::mutex mutex;
		mu_coin::endpoint self;
        boost::multi_index_container
        <peer_information,
            boost::multi_index::indexed_by
            <
                boost::multi_index::hashed_unique <boost::multi_index::member <peer_information, mu_coin::endpoint, &peer_information::endpoint>>,
                boost::multi_index::ordered_non_unique <boost::multi_index::member <peer_information, std::chrono::system_clock::time_point, &peer_information::last_contact>>,
                boost::multi_index::ordered_non_unique <boost::multi_index::member <peer_information, std::chrono::system_clock::time_point, &peer_information::last_attempt>, std::greater <std::chrono::system_clock::time_point>>
            >
        > peers;
    };
    extern mu_coin::keypair test_genesis_key;
    extern mu_coin::address genesis_address;
    class genesis
    {
    public:
        explicit genesis ();
        void initialize (mu_coin::block_store &) const;
        mu_coin::block_hash hash () const;
        mu_coin::send_block send1;
        mu_coin::send_block send2;
        mu_coin::open_block open;
    };
    class log
    {
    public:
        log ();
        void add (std::string const &);
        void dump_cerr ();
        boost::circular_buffer <std::pair <std::chrono::system_clock::time_point, std::string>> items;
    };
    class client : public std::enable_shared_from_this <mu_coin::client>
    {
    public:
        client (boost::shared_ptr <boost::asio::io_service>, boost::shared_ptr <boost::network::utils::thread_pool>, uint16_t, uint16_t, boost::filesystem::path const &, mu_coin::processor_service &, mu_coin::address const &);
        client (boost::shared_ptr <boost::asio::io_service>, boost::shared_ptr <boost::network::utils::thread_pool>, uint16_t, uint16_t, mu_coin::processor_service &, mu_coin::address const &);
        ~client ();
        bool send (mu_coin::public_key const &, mu_coin::uint256_t const &);
        mu_coin::uint256_t balance ();
        void start ();
        void stop ();
        std::shared_ptr <mu_coin::client> shared ();
        bool is_representative ();
		void representative_vote (mu_coin::votes &, mu_coin::block const &);
        mu_coin::log log;
        mu_coin::address representative;
        mu_coin::block_store store;
        mu_coin::gap_cache gap_cache;
        mu_coin::ledger ledger;
        mu_coin::conflicts conflicts;
        mu_coin::wallet wallet;
        mu_coin::network network;
        mu_coin::bootstrap_receiver bootstrap;
        mu_coin::rpc rpc;
        mu_coin::processor processor;
        mu_coin::transactions transactions;
        mu_coin::peer_container peers;
        mu_coin::processor_service & service;
    };
    class system
    {
    public:
        system (size_t, uint16_t, uint16_t, size_t);
        ~system ();
        void generate_activity (mu_coin::client &);
        void generate_mass_activity (uint32_t, mu_coin::client &);
        void generate_usage_traffic (uint32_t, uint32_t, size_t);
        void generate_usage_traffic (uint32_t, uint32_t);
        mu_coin::uint256_t get_random_amount (mu_coin::client &);
        void generate_send_new (mu_coin::client &);
        void generate_send_existing (mu_coin::client &);
        boost::shared_ptr <boost::asio::io_service> service;
        boost::shared_ptr <boost::network::utils::thread_pool> pool;
        mu_coin::processor_service processor;
        std::vector <std::shared_ptr <mu_coin::client>> clients;
    };
}