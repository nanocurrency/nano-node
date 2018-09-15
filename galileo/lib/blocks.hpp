#pragma once

#include <galileo/lib/numbers.hpp>

#include <assert.h>
#include <blake2/blake2.h>
#include <boost/property_tree/json_parser.hpp>
#include <streambuf>

namespace galileo
{
std::string to_string_hex (uint64_t);
bool from_string_hex (std::string const &, uint64_t &);
// We operate on streams of uint8_t by convention
using stream = std::basic_streambuf<uint8_t>;
// Read a raw byte stream the size of `T' and fill value.
template <typename T>
bool read (galileo::stream & stream_a, T & value)
{
	static_assert (std::is_pod<T>::value, "Can't stream read non-standard layout types");
	auto amount_read (stream_a.sgetn (reinterpret_cast<uint8_t *> (&value), sizeof (value)));
	return amount_read != sizeof (value);
}
template <typename T>
void write (galileo::stream & stream_a, T const & value)
{
	static_assert (std::is_pod<T>::value, "Can't stream write non-standard layout types");
	auto amount_written (stream_a.sputn (reinterpret_cast<uint8_t const *> (&value), sizeof (value)));
	assert (amount_written == sizeof (value));
}
class block_visitor;
enum class block_type : uint8_t
{
	invalid = 0,
	not_a_block = 1,
	send = 2,
	receive = 3,
	open = 4,
	change = 5,
	state = 6
};
class block
{
public:
	// Return a digest of the hashables in this block.
	galileo::block_hash hash () const;
	std::string to_json ();
	virtual void hash (blake2b_state &) const = 0;
	virtual uint64_t block_work () const = 0;
	virtual void block_work_set (uint64_t) = 0;
	// Previous block in account's chain, zero for open block
	virtual galileo::block_hash previous () const = 0;
	// Source block for open/receive blocks, zero otherwise.
	virtual galileo::block_hash source () const = 0;
	// Previous block or account number for open blocks
	virtual galileo::block_hash root () const = 0;
	virtual galileo::account representative () const = 0;
	virtual void serialize (galileo::stream &) const = 0;
	virtual void serialize_json (std::string &) const = 0;
	virtual void visit (galileo::block_visitor &) const = 0;
	virtual bool operator== (galileo::block const &) const = 0;
	virtual galileo::block_type type () const = 0;
	virtual galileo::signature block_signature () const = 0;
	virtual void signature_set (galileo::uint512_union const &) = 0;
	virtual ~block () = default;
	virtual bool valid_predecessor (galileo::block const &) const = 0;
};
class send_hashables
{
public:
	send_hashables (galileo::account const &, galileo::block_hash const &, galileo::amount const &);
	send_hashables (bool &, galileo::stream &);
	send_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	galileo::block_hash previous;
	galileo::account destination;
	galileo::amount balance;
};
class send_block : public galileo::block
{
public:
	send_block (galileo::block_hash const &, galileo::account const &, galileo::amount const &, galileo::raw_key const &, galileo::public_key const &, uint64_t);
	send_block (bool &, galileo::stream &);
	send_block (bool &, boost::property_tree::ptree const &);
	virtual ~send_block () = default;
	using galileo::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	galileo::block_hash previous () const override;
	galileo::block_hash source () const override;
	galileo::block_hash root () const override;
	galileo::account representative () const override;
	void serialize (galileo::stream &) const override;
	void serialize_json (std::string &) const override;
	bool deserialize (galileo::stream &);
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (galileo::block_visitor &) const override;
	galileo::block_type type () const override;
	galileo::signature block_signature () const override;
	void signature_set (galileo::uint512_union const &) override;
	bool operator== (galileo::block const &) const override;
	bool operator== (galileo::send_block const &) const;
	bool valid_predecessor (galileo::block const &) const override;
	static size_t constexpr size = sizeof (galileo::account) + sizeof (galileo::block_hash) + sizeof (galileo::amount) + sizeof (galileo::signature) + sizeof (uint64_t);
	send_hashables hashables;
	galileo::signature signature;
	uint64_t work;
};
class receive_hashables
{
public:
	receive_hashables (galileo::block_hash const &, galileo::block_hash const &);
	receive_hashables (bool &, galileo::stream &);
	receive_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	galileo::block_hash previous;
	galileo::block_hash source;
};
class receive_block : public galileo::block
{
public:
	receive_block (galileo::block_hash const &, galileo::block_hash const &, galileo::raw_key const &, galileo::public_key const &, uint64_t);
	receive_block (bool &, galileo::stream &);
	receive_block (bool &, boost::property_tree::ptree const &);
	virtual ~receive_block () = default;
	using galileo::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	galileo::block_hash previous () const override;
	galileo::block_hash source () const override;
	galileo::block_hash root () const override;
	galileo::account representative () const override;
	void serialize (galileo::stream &) const override;
	void serialize_json (std::string &) const override;
	bool deserialize (galileo::stream &);
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (galileo::block_visitor &) const override;
	galileo::block_type type () const override;
	galileo::signature block_signature () const override;
	void signature_set (galileo::uint512_union const &) override;
	bool operator== (galileo::block const &) const override;
	bool operator== (galileo::receive_block const &) const;
	bool valid_predecessor (galileo::block const &) const override;
	static size_t constexpr size = sizeof (galileo::block_hash) + sizeof (galileo::block_hash) + sizeof (galileo::signature) + sizeof (uint64_t);
	receive_hashables hashables;
	galileo::signature signature;
	uint64_t work;
};
class open_hashables
{
public:
	open_hashables (galileo::block_hash const &, galileo::account const &, galileo::account const &);
	open_hashables (bool &, galileo::stream &);
	open_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	galileo::block_hash source;
	galileo::account representative;
	galileo::account account;
};
class open_block : public galileo::block
{
public:
	open_block (galileo::block_hash const &, galileo::account const &, galileo::account const &, galileo::raw_key const &, galileo::public_key const &, uint64_t);
	open_block (galileo::block_hash const &, galileo::account const &, galileo::account const &, std::nullptr_t);
	open_block (bool &, galileo::stream &);
	open_block (bool &, boost::property_tree::ptree const &);
	virtual ~open_block () = default;
	using galileo::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	galileo::block_hash previous () const override;
	galileo::block_hash source () const override;
	galileo::block_hash root () const override;
	galileo::account representative () const override;
	void serialize (galileo::stream &) const override;
	void serialize_json (std::string &) const override;
	bool deserialize (galileo::stream &);
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (galileo::block_visitor &) const override;
	galileo::block_type type () const override;
	galileo::signature block_signature () const override;
	void signature_set (galileo::uint512_union const &) override;
	bool operator== (galileo::block const &) const override;
	bool operator== (galileo::open_block const &) const;
	bool valid_predecessor (galileo::block const &) const override;
	static size_t constexpr size = sizeof (galileo::block_hash) + sizeof (galileo::account) + sizeof (galileo::account) + sizeof (galileo::signature) + sizeof (uint64_t);
	galileo::open_hashables hashables;
	galileo::signature signature;
	uint64_t work;
};
class change_hashables
{
public:
	change_hashables (galileo::block_hash const &, galileo::account const &);
	change_hashables (bool &, galileo::stream &);
	change_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	galileo::block_hash previous;
	galileo::account representative;
};
class change_block : public galileo::block
{
public:
	change_block (galileo::block_hash const &, galileo::account const &, galileo::raw_key const &, galileo::public_key const &, uint64_t);
	change_block (bool &, galileo::stream &);
	change_block (bool &, boost::property_tree::ptree const &);
	virtual ~change_block () = default;
	using galileo::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	galileo::block_hash previous () const override;
	galileo::block_hash source () const override;
	galileo::block_hash root () const override;
	galileo::account representative () const override;
	void serialize (galileo::stream &) const override;
	void serialize_json (std::string &) const override;
	bool deserialize (galileo::stream &);
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (galileo::block_visitor &) const override;
	galileo::block_type type () const override;
	galileo::signature block_signature () const override;
	void signature_set (galileo::uint512_union const &) override;
	bool operator== (galileo::block const &) const override;
	bool operator== (galileo::change_block const &) const;
	bool valid_predecessor (galileo::block const &) const override;
	static size_t constexpr size = sizeof (galileo::block_hash) + sizeof (galileo::account) + sizeof (galileo::signature) + sizeof (uint64_t);
	galileo::change_hashables hashables;
	galileo::signature signature;
	uint64_t work;
};
class state_hashables
{
public:
	state_hashables (galileo::account const &, galileo::block_hash const &, galileo::account const &, galileo::amount const &, galileo::uint256_union const &);
	state_hashables (bool &, galileo::stream &);
	state_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	// Account# / public key that operates this account
	// Uses:
	// Bulk signature validation in advance of further ledger processing
	// Arranging uncomitted transactions by account
	galileo::account account;
	// Previous transaction in this chain
	galileo::block_hash previous;
	// Representative of this account
	galileo::account representative;
	// Current balance of this account
	// Allows lookup of account balance simply by looking at the head block
	galileo::amount balance;
	// Link field contains source block_hash if receiving, destination account if sending
	galileo::uint256_union link;
};
class state_block : public galileo::block
{
public:
	state_block (galileo::account const &, galileo::block_hash const &, galileo::account const &, galileo::amount const &, galileo::uint256_union const &, galileo::raw_key const &, galileo::public_key const &, uint64_t);
	state_block (bool &, galileo::stream &);
	state_block (bool &, boost::property_tree::ptree const &);
	virtual ~state_block () = default;
	using galileo::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	galileo::block_hash previous () const override;
	galileo::block_hash source () const override;
	galileo::block_hash root () const override;
	galileo::account representative () const override;
	void serialize (galileo::stream &) const override;
	void serialize_json (std::string &) const override;
	bool deserialize (galileo::stream &);
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (galileo::block_visitor &) const override;
	galileo::block_type type () const override;
	galileo::signature block_signature () const override;
	void signature_set (galileo::uint512_union const &) override;
	bool operator== (galileo::block const &) const override;
	bool operator== (galileo::state_block const &) const;
	bool valid_predecessor (galileo::block const &) const override;
	static size_t constexpr size = sizeof (galileo::account) + sizeof (galileo::block_hash) + sizeof (galileo::account) + sizeof (galileo::amount) + sizeof (galileo::uint256_union) + sizeof (galileo::signature) + sizeof (uint64_t);
	galileo::state_hashables hashables;
	galileo::signature signature;
	uint64_t work;
};
class block_visitor
{
public:
	virtual void send_block (galileo::send_block const &) = 0;
	virtual void receive_block (galileo::receive_block const &) = 0;
	virtual void open_block (galileo::open_block const &) = 0;
	virtual void change_block (galileo::change_block const &) = 0;
	virtual void state_block (galileo::state_block const &) = 0;
	virtual ~block_visitor () = default;
};
std::unique_ptr<galileo::block> deserialize_block (galileo::stream &);
std::unique_ptr<galileo::block> deserialize_block (galileo::stream &, galileo::block_type);
std::unique_ptr<galileo::block> deserialize_block_json (boost::property_tree::ptree const &);
void serialize_block (galileo::stream &, galileo::block const &);
}
