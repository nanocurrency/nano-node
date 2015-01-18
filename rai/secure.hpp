#pragma once 

#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/filesystem.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/multiprecision/cpp_int.hpp>

#include <cryptopp/osrng.h>
#include <cryptopp/sha3.h>

#include <leveldb/db.h>

#include <ed25519-donna/ed25519.h>

#include <unordered_map>

namespace CryptoPP
{
    class SHA3;
}

namespace rai
{
    enum class rai_networks
    {
        rai_test_network,
        rai_beta_network,
        rai_live_network
    };
    rai::rai_networks const rai_network = rai_networks::ACTIVE_NETWORK;
    extern CryptoPP::AutoSeededRandomPool random_pool;
    using stream = std::basic_streambuf <uint8_t>;
    using bufferstream = boost::iostreams::stream_buffer <boost::iostreams::basic_array_source <uint8_t>>;
    using vectorstream = boost::iostreams::stream_buffer <boost::iostreams::back_insert_device <std::vector <uint8_t>>>;
	boost::filesystem::path working_path ();
	boost::filesystem::path unique_path ();
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
    std::string to_string_hex (uint64_t);
    bool from_string_hex (std::string const &, uint64_t &);
	using uint128_t = boost::multiprecision::uint128_t;
	using uint256_t = boost::multiprecision::uint256_t;
	using uint512_t = boost::multiprecision::uint512_t;
    // 10^20 chosen so that the amount will fit in a 64bit
    // Base 10 reduction so scaling is intuitive to users
    rai::uint128_t const scale_64bit_base10 = rai::uint128_t ("100000000000000000000");
    uint64_t scale_down (rai::uint128_t const &);
    rai::uint128_t scale_up (uint64_t);
	union uint128_union
	{
	public:
		uint128_union () = default;
        uint128_union (uint64_t);
		uint128_union (rai::uint128_union const &) = default;
		uint128_union (rai::uint128_t const &);
        bool operator == (rai::uint128_union const &) const;
        void encode_hex (std::string &) const;
        bool decode_hex (std::string const &);
        void encode_dec (std::string &) const;
        bool decode_dec (std::string const &);
        rai::uint128_t number () const;
        void clear ();
        bool is_zero () const;
		std::array <uint8_t, 16> bytes;
        std::array <char, 16> chars;
        std::array <uint32_t, 4> dwords;
		std::array <uint64_t, 2> qwords;
	};
	using amount = uint128_union;
	union uint256_union
	{
		uint256_union () = default;
		uint256_union (std::string const &);
		uint256_union (uint64_t, uint64_t = 0, uint64_t = 0, uint64_t = 0);
		uint256_union (rai::uint256_t const &);
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
		std::string to_base58check () const;
		bool decode_base58check (std::string const &);
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
	using account = uint256_union;
	using balance = uint256_union;
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
		void encode_hex (std::string &) const;
		bool decode_hex (std::string const &);
		std::array <uint8_t, 64> bytes;
		std::array <uint32_t, 16> dwords;
		std::array <uint64_t, 8> qwords;
		std::array <uint256_union, 2> uint256s;
		void clear ();
		boost::multiprecision::uint512_t number () const;
	};
	using signature = uint512_union;
	class keypair
	{
	public:
		keypair ();
		keypair (std::string const &);
		rai::public_key pub;
		rai::private_key prv;
	};
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
}
namespace boost
{
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
namespace rai
{
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
        virtual uint64_t block_work () const = 0;
        virtual void block_work_set (uint64_t) = 0;
		virtual rai::block_hash previous () const = 0;
		virtual rai::block_hash source () const = 0;
		virtual rai::block_hash root () const = 0;
		virtual void serialize (rai::stream &) const = 0;
        virtual void serialize_json (std::string &) const = 0;
		virtual void visit (rai::block_visitor &) const = 0;
		virtual bool operator == (rai::block const &) const = 0;
		virtual std::unique_ptr <rai::block> clone () const = 0;
        virtual rai::block_type type () const = 0;
        static size_t const publish_test_work = 1024;
        static size_t const publish_full_work = 8 * 1024; // 8 * 8 * 1024 = 64k to generate work
        static size_t const publish_work = rai::rai_network == rai::rai_networks::rai_test_network ? publish_test_work : publish_full_work;
        static uint64_t const publish_test_threshold = 0xff00000000000000;
        static uint64_t const publish_full_threshold = 0xfffffc0000000000;
        static uint64_t const publish_threshold = rai::rai_network == rai::rai_networks::rai_test_network ? publish_test_threshold : publish_full_threshold;
    };
    class unique_ptr_block_hash
    {
    public:
		size_t operator () (std::unique_ptr <rai::block> const &) const;
		bool operator () (std::unique_ptr <rai::block> const &, std::unique_ptr <rai::block> const &) const;
    };
	void sign_message (rai::private_key const &, rai::public_key const &, rai::uint256_union const &, rai::uint512_union &);
	bool validate_message (rai::public_key const &, rai::uint256_union const &, rai::uint512_union const &);
    std::unique_ptr <rai::block> deserialize_block (rai::stream &);
    std::unique_ptr <rai::block> deserialize_block (rai::stream &, rai::block_type);
    std::unique_ptr <rai::block> deserialize_block_json (boost::property_tree::ptree const &);
    void serialize_block (rai::stream &, rai::block const &);
    void work_generate (rai::block &);
    uint64_t work_generate (rai::block_hash const &);
    bool work_validate (rai::block &);
    bool work_validate (rai::block_hash const &, uint64_t);
	class send_hashables
	{
	public:
		void hash (CryptoPP::SHA3 &) const;
		rai::account destination;
		rai::block_hash previous;
		rai::amount balance;
	};
	class send_block : public rai::block
	{
	public:
		send_block () = default;
		send_block (send_block const &);
		using rai::block::hash;
        void hash (CryptoPP::SHA3 &) const override;
        uint64_t block_work () const override;
        void block_work_set (uint64_t) override;
		rai::block_hash previous () const override;
		rai::block_hash source () const override;
		rai::block_hash root () const override;
        void serialize (rai::stream &) const override;
        void serialize_json (std::string &) const override;
        bool deserialize (rai::stream &);
        bool deserialize_json (boost::property_tree::ptree const &);
		void visit (rai::block_visitor &) const override;
		std::unique_ptr <rai::block> clone () const override;
        rai::block_type type () const override;
		bool operator == (rai::block const &) const override;
		bool operator == (rai::send_block const &) const;
        static size_t constexpr size = sizeof (rai::account) + sizeof (rai::block_hash) + sizeof (rai::amount) + sizeof (rai::signature) + sizeof (uint64_t);
		send_hashables hashables;
		rai::signature signature;
        uint64_t work;
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
        uint64_t block_work () const override;
        void block_work_set (uint64_t) override;
		rai::block_hash previous () const override;
		rai::block_hash source () const override;
		rai::block_hash root () const override;
        void serialize (rai::stream &) const override;
        void serialize_json (std::string &) const override;
		bool deserialize (rai::stream &);
        bool deserialize_json (boost::property_tree::ptree const &);
		void visit (rai::block_visitor &) const override;
		std::unique_ptr <rai::block> clone () const override;
		rai::block_type type () const override;
		bool operator == (rai::block const &) const override;
        bool operator == (rai::receive_block const &) const;
        static size_t constexpr size = sizeof (rai::block_hash) + sizeof (rai::block_hash) + sizeof (rai::signature) + sizeof (uint64_t);
        receive_hashables hashables;
        rai::signature signature;
        uint64_t work;
	};
	class open_hashables
	{
	public:
		void hash (CryptoPP::SHA3 &) const;
        rai::account account;
		rai::account representative;
		rai::block_hash source;
	};
	class open_block : public rai::block
	{
	public:
		using rai::block::hash;
        void hash (CryptoPP::SHA3 &) const override;
        uint64_t block_work () const override;
        void block_work_set (uint64_t) override;
		rai::block_hash previous () const override;
		rai::block_hash source () const override;
		rai::block_hash root () const override;
        void serialize (rai::stream &) const override;
        void serialize_json (std::string &) const override;
        bool deserialize (rai::stream &);
        bool deserialize_json (boost::property_tree::ptree const &);
		void visit (rai::block_visitor &) const override;
		std::unique_ptr <rai::block> clone () const override;
		rai::block_type type () const override;
		bool operator == (rai::block const &) const override;
        bool operator == (rai::open_block const &) const;
        static size_t constexpr size = sizeof (rai::account) + sizeof (rai::account) + sizeof (rai::block_hash) + sizeof (rai::signature) + sizeof (uint64_t);
        rai::open_hashables hashables;
		rai::signature signature;
		uint64_t work;
	};
	class change_hashables
	{
	public:
        change_hashables (rai::account const &, rai::block_hash const &);
        change_hashables (bool &, rai::stream &);
        change_hashables (bool &, boost::property_tree::ptree const &);
		void hash (CryptoPP::SHA3 &) const;
		rai::account representative;
		rai::block_hash previous;
	};
	class change_block : public rai::block
	{
    public:
        change_block (rai::account const &, rai::block_hash const &, uint64_t, rai::private_key const &, rai::public_key const &);
        change_block (rai::account const &, rai::block_hash const &, rai::private_key const &, rai::public_key const &);
        change_block (bool &, rai::stream &);
        change_block (bool &, boost::property_tree::ptree const &);
		using rai::block::hash;
        void hash (CryptoPP::SHA3 &) const override;
        uint64_t block_work () const override;
        void block_work_set (uint64_t) override;
		rai::block_hash previous () const override;
		rai::block_hash source () const override;
		rai::block_hash root () const override;
        void serialize (rai::stream &) const override;
        void serialize_json (std::string &) const override;
        bool deserialize (rai::stream &);
        bool deserialize_json (boost::property_tree::ptree const &);
		void visit (rai::block_visitor &) const override;
		std::unique_ptr <rai::block> clone () const override;
		rai::block_type type () const override;
		bool operator == (rai::block const &) const override;
        bool operator == (rai::change_block const &) const;
        static size_t constexpr size = sizeof (rai::account) + sizeof (rai::block_hash) + sizeof (rai::signature) + sizeof (uint64_t);
        rai::change_hashables hashables;
		rai::signature signature;
        uint64_t work;
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
        bool operator != (rai::frontier const &) const;
		rai::uint256_union hash;
		rai::account representative;
		rai::uint128_union balance;
		uint64_t time;
	};
	class account_entry
	{
	public:
		account_entry * operator -> ();
		rai::account first;
		rai::frontier second;
	};
	class account_iterator
	{
	public:
		account_iterator (leveldb::DB &);
		account_iterator (leveldb::DB &, std::nullptr_t);
		account_iterator (leveldb::DB &, rai::account const &);
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
    class receivable
    {
    public:
        void serialize (rai::stream &) const;
        bool deserialize (rai::stream &);
        bool operator == (rai::receivable const &) const;
        rai::account source;
        rai::amount amount;
        rai::account destination;
    };
    class pending_entry
    {
    public:
        pending_entry * operator -> ();
        rai::account first;
        rai::receivable second;
    };
    class pending_iterator
    {
    public:
        pending_iterator (leveldb::DB &);
        pending_iterator (leveldb::DB &, std::nullptr_t);
        pending_iterator (rai::pending_iterator &&) = default;
        pending_iterator & operator ++ ();
        pending_iterator & operator = (rai::pending_iterator &&) = default;
        pending_entry & operator -> ();
        bool operator == (rai::pending_iterator const &) const;
        bool operator != (rai::pending_iterator const &) const;
        void set_current ();
        std::unique_ptr <leveldb::Iterator> iterator;
        rai::pending_entry current;
    };
    class hash_iterator
    {
    public:
        hash_iterator (leveldb::DB &);
        hash_iterator (leveldb::DB &, std::nullptr_t);
        hash_iterator (rai::hash_iterator &&) = default;
        hash_iterator & operator ++ ();
        hash_iterator & operator = (rai::hash_iterator &&) = default;
        rai::block_hash & operator * ();
        bool operator == (rai::hash_iterator const &) const;
        bool operator != (rai::hash_iterator const &) const;
        void set_current ();
        std::unique_ptr <leveldb::Iterator> iterator;
        rai::block_hash current;
    };
	extern block_store_temp_t block_store_temp;
	class block_store
	{
	public:
        block_store (leveldb::Status &, block_store_temp_t const &);
        block_store (leveldb::Status &, boost::filesystem::path const &);
		uint64_t now ();
		
		void block_put (rai::block_hash const &, rai::block const &);
		std::unique_ptr <rai::block> block_get (rai::block_hash const &);
		void block_del (rai::block_hash const &);
		bool block_exists (rai::block_hash const &);
		rai::block_iterator blocks_begin ();
		rai::block_iterator blocks_end ();
		
		void latest_put (rai::account const &, rai::frontier const &);
		bool latest_get (rai::account const &, rai::frontier &);
		void latest_del (rai::account const &);
		bool latest_exists (rai::account const &);
		rai::account_iterator latest_begin (rai::account const &);
		rai::account_iterator latest_begin ();
		rai::account_iterator latest_end ();
		
        void pending_put (rai::block_hash const &, rai::receivable const &);
		void pending_del (rai::block_hash const &);
		bool pending_get (rai::block_hash const &, rai::receivable &);
        bool pending_exists (rai::block_hash const &);
        rai::pending_iterator pending_begin ();
        rai::pending_iterator pending_end ();
		
		rai::uint128_t representation_get (rai::account const &);
		void representation_put (rai::account const &, rai::uint128_t const &);
		
		void unchecked_put (rai::block_hash const &, rai::block const &);
		std::unique_ptr <rai::block> unchecked_get (rai::block_hash const &);
		void unchecked_del (rai::block_hash const &);
        rai::block_iterator unchecked_begin ();
        rai::block_iterator unchecked_end ();
        
        void unsynced_put (rai::block_hash const &);
        void unsynced_del (rai::block_hash const &);
        bool unsynced_exists (rai::block_hash const &);
        rai::hash_iterator unsynced_begin ();
        rai::hash_iterator unsynced_end ();

        void stack_open ();
        void stack_push (uint64_t, rai::block_hash const &);
        rai::block_hash stack_pop (uint64_t);
		
		void checksum_put (uint64_t, uint8_t, rai::checksum const &);
		bool checksum_get (uint64_t, uint8_t, rai::checksum &);
		void checksum_del (uint64_t, uint8_t);
        
        void clear (leveldb::DB &);
		
	private:
		// account -> block_hash, representative, balance, timestamp    // Account to frontier block, representative, balance, last_change
		std::unique_ptr <leveldb::DB> accounts;
		// block_hash -> block                                          // Mapping block hash to contents
		std::unique_ptr <leveldb::DB> blocks;
		// block_hash -> sender, amount, destination                    // Pending blocks to sender account, amount, destination account
		std::unique_ptr <leveldb::DB> pending;
		// account -> weight                                            // Representation
		std::unique_ptr <leveldb::DB> representation;
		// block_hash -> block                                          // Unchecked bootstrap blocks
		std::unique_ptr <leveldb::DB> unchecked;
        // block_hash ->                                                // Blocks that haven't been broadcast
        std::unique_ptr <leveldb::DB> unsynced;
        // uint64_t -> block_hash                                       // Block dependency stack while bootstrapping
        std::unique_ptr <leveldb::DB> stack;
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
		fork_previous, // Malicious fork based on previous
        fork_source, // Malicious fork based on source
		gap_previous, // Block marked as previous isn't in store
		gap_source, // Block marked as source isn't in store
		not_receive_from_send, // Receive does not have a send source
        account_mismatch // Account number in open block doesn't match send destination
    };
	class vote
	{
	public:
		rai::uint256_union hash () const;
		rai::account account;
		rai::signature signature;
		uint64_t sequence;
		std::unique_ptr <rai::block> block;
	};
	class votes
	{
	public:
		votes (rai::block_hash const &);
		bool vote (rai::vote const &);
		uint64_t sequence;
		rai::block_hash id;
		std::unordered_map <rai::account, std::pair <uint64_t, std::unique_ptr <rai::block>>> rep_votes;
    };
    class kdf
    {
    public:
        kdf (size_t);
        rai::uint256_union generate (std::string const &, rai::uint256_union const &);
        size_t const entries;
        std::unique_ptr <uint64_t []> data;
    };
	class ledger
	{
	public:
		ledger (bool &, leveldb::Status const &, rai::block_store &);
		std::pair <rai::uint128_t, std::unique_ptr <rai::block>> winner (rai::votes const & votes_a);
		std::map <rai::uint128_t, std::unique_ptr <rai::block>, std::greater <rai::uint128_t>> tally (rai::votes const &);
		rai::account account (rai::block_hash const &);
		rai::uint128_t amount (rai::block_hash const &);
		rai::uint128_t balance (rai::block_hash const &);
		rai::uint128_t account_balance (rai::account const &);
        rai::uint128_t weight (rai::account const &);
		std::unique_ptr <rai::block> successor (rai::block_hash const &);
		rai::block_hash latest (rai::account const &);
        rai::block_hash latest_root (rai::account const &);
		rai::account representative (rai::block_hash const &);
		rai::account representative_calculated (rai::block_hash const &);
		rai::account representative_cached (rai::block_hash const &);
		rai::uint128_t supply ();
		rai::process_result process (rai::block const &);
		void rollback (rai::block_hash const &);
		void change_latest (rai::account const &, rai::block_hash const &, rai::account const &, rai::uint128_union const &);
		void move_representation (rai::account const &, rai::account const &, rai::uint128_t const &);
		void checksum_update (rai::block_hash const &);
		rai::checksum checksum (rai::account const &, rai::account const &);
        void dump_account_chain (rai::account const &);
        rai::block_store & store;
        std::function <void (rai::send_block const &, rai::account const &, rai::amount const &)> send_observer;
        std::function <void (rai::receive_block const &, rai::account const &, rai::amount const &)> receive_observer;
        std::function <void (rai::open_block const &, rai::account const &, rai::amount const &, rai::account const &)> open_observer;
        std::function <void (rai::change_block const &, rai::account const &, rai::account const &)> change_observer;
	};
    extern rai::keypair const test_genesis_key;
    extern rai::account const rai_test_account;
    extern rai::account const rai_beta_account;
    extern rai::account const rai_live_account;
    extern rai::account const genesis_account;
    class genesis
    {
    public:
        explicit genesis ();
        void initialize (rai::block_store &) const;
        rai::block_hash hash () const;
        rai::open_block open;
    };
}