#pragma once

#include <banano/lib/numbers.hpp>

#include <assert.h>
#include <blake2/blake2.h>
#include <boost/property_tree/json_parser.hpp>
#include <streambuf>

namespace rai
{
std::string to_string_hex (uint64_t);
bool from_string_hex (std::string const &, uint64_t &);
// We operate on streams of uint8_t by convention
using stream = std::basic_streambuf<uint8_t>;
// Read a raw byte stream the size of `T' and fill value.
template <typename T>
bool read (rai::stream & stream_a, T & value)
{
	static_assert (std::is_pod<T>::value, "Can't stream read non-standard layout types");
	auto amount_read (stream_a.sgetn (reinterpret_cast<uint8_t *> (&value), sizeof (value)));
	return amount_read != sizeof (value);
}
template <typename T>
void write (rai::stream & stream_a, T const & value)
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
	rai::block_hash hash () const;
	std::string to_json ();
	virtual void hash (blake2b_state &) const = 0;
	virtual uint64_t block_work () const = 0;
	virtual void block_work_set (uint64_t) = 0;
	// Previous block in account's chain, zero for open block
	virtual rai::block_hash previous () const = 0;
	// Source block for open/receive blocks, zero otherwise.
	virtual rai::block_hash source () const = 0;
	// Previous block or account number for open blocks
	virtual rai::block_hash root () const = 0;
	virtual rai::account representative () const = 0;
	virtual void serialize (rai::stream &) const = 0;
	virtual void serialize_json (std::string &) const = 0;
	virtual void visit (rai::block_visitor &) const = 0;
	virtual bool operator== (rai::block const &) const = 0;
	virtual rai::block_type type () const = 0;
	virtual rai::signature block_signature () const = 0;
	virtual void signature_set (rai::uint512_union const &) = 0;
	virtual ~block () = default;
	virtual bool valid_predecessor (rai::block const &) const = 0;
};
class send_hashables
{
public:
	send_hashables (rai::account const &, rai::block_hash const &, rai::amount const &);
	send_hashables (bool &, rai::stream &);
	send_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	rai::block_hash previous;
	rai::account destination;
	rai::amount balance;
};
class send_block : public rai::block
{
public:
	send_block (rai::block_hash const &, rai::account const &, rai::amount const &, rai::raw_key const &, rai::public_key const &, uint64_t);
	send_block (bool &, rai::stream &);
	send_block (bool &, boost::property_tree::ptree const &);
	virtual ~send_block () = default;
	using rai::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	rai::block_hash previous () const override;
	rai::block_hash source () const override;
	rai::block_hash root () const override;
	rai::account representative () const override;
	void serialize (rai::stream &) const override;
	void serialize_json (std::string &) const override;
	bool deserialize (rai::stream &);
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (rai::block_visitor &) const override;
	rai::block_type type () const override;
	rai::signature block_signature () const override;
	void signature_set (rai::uint512_union const &) override;
	bool operator== (rai::block const &) const override;
	bool operator== (rai::send_block const &) const;
	bool valid_predecessor (rai::block const &) const override;
	static size_t constexpr size = sizeof (rai::account) + sizeof (rai::block_hash) + sizeof (rai::amount) + sizeof (rai::signature) + sizeof (uint64_t);
	send_hashables hashables;
	rai::signature signature;
	uint64_t work;
};
class receive_hashables
{
public:
	receive_hashables (rai::block_hash const &, rai::block_hash const &);
	receive_hashables (bool &, rai::stream &);
	receive_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	rai::block_hash previous;
	rai::block_hash source;
};
class receive_block : public rai::block
{
public:
	receive_block (rai::block_hash const &, rai::block_hash const &, rai::raw_key const &, rai::public_key const &, uint64_t);
	receive_block (bool &, rai::stream &);
	receive_block (bool &, boost::property_tree::ptree const &);
	virtual ~receive_block () = default;
	using rai::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	rai::block_hash previous () const override;
	rai::block_hash source () const override;
	rai::block_hash root () const override;
	rai::account representative () const override;
	void serialize (rai::stream &) const override;
	void serialize_json (std::string &) const override;
	bool deserialize (rai::stream &);
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (rai::block_visitor &) const override;
	rai::block_type type () const override;
	rai::signature block_signature () const override;
	void signature_set (rai::uint512_union const &) override;
	bool operator== (rai::block const &) const override;
	bool operator== (rai::receive_block const &) const;
	bool valid_predecessor (rai::block const &) const override;
	static size_t constexpr size = sizeof (rai::block_hash) + sizeof (rai::block_hash) + sizeof (rai::signature) + sizeof (uint64_t);
	receive_hashables hashables;
	rai::signature signature;
	uint64_t work;
};
class open_hashables
{
public:
	open_hashables (rai::block_hash const &, rai::account const &, rai::account const &);
	open_hashables (bool &, rai::stream &);
	open_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	rai::block_hash source;
	rai::account representative;
	rai::account account;
};
class open_block : public rai::block
{
public:
	open_block (rai::block_hash const &, rai::account const &, rai::account const &, rai::raw_key const &, rai::public_key const &, uint64_t);
	open_block (rai::block_hash const &, rai::account const &, rai::account const &, std::nullptr_t);
	open_block (bool &, rai::stream &);
	open_block (bool &, boost::property_tree::ptree const &);
	virtual ~open_block () = default;
	using rai::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	rai::block_hash previous () const override;
	rai::block_hash source () const override;
	rai::block_hash root () const override;
	rai::account representative () const override;
	void serialize (rai::stream &) const override;
	void serialize_json (std::string &) const override;
	bool deserialize (rai::stream &);
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (rai::block_visitor &) const override;
	rai::block_type type () const override;
	rai::signature block_signature () const override;
	void signature_set (rai::uint512_union const &) override;
	bool operator== (rai::block const &) const override;
	bool operator== (rai::open_block const &) const;
	bool valid_predecessor (rai::block const &) const override;
	static size_t constexpr size = sizeof (rai::block_hash) + sizeof (rai::account) + sizeof (rai::account) + sizeof (rai::signature) + sizeof (uint64_t);
	rai::open_hashables hashables;
	rai::signature signature;
	uint64_t work;
};
class change_hashables
{
public:
	change_hashables (rai::block_hash const &, rai::account const &);
	change_hashables (bool &, rai::stream &);
	change_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	rai::block_hash previous;
	rai::account representative;
};
class change_block : public rai::block
{
public:
	change_block (rai::block_hash const &, rai::account const &, rai::raw_key const &, rai::public_key const &, uint64_t);
	change_block (bool &, rai::stream &);
	change_block (bool &, boost::property_tree::ptree const &);
	virtual ~change_block () = default;
	using rai::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	rai::block_hash previous () const override;
	rai::block_hash source () const override;
	rai::block_hash root () const override;
	rai::account representative () const override;
	void serialize (rai::stream &) const override;
	void serialize_json (std::string &) const override;
	bool deserialize (rai::stream &);
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (rai::block_visitor &) const override;
	rai::block_type type () const override;
	rai::signature block_signature () const override;
	void signature_set (rai::uint512_union const &) override;
	bool operator== (rai::block const &) const override;
	bool operator== (rai::change_block const &) const;
	bool valid_predecessor (rai::block const &) const override;
	static size_t constexpr size = sizeof (rai::block_hash) + sizeof (rai::account) + sizeof (rai::signature) + sizeof (uint64_t);
	rai::change_hashables hashables;
	rai::signature signature;
	uint64_t work;
};
class state_hashables
{
public:
	state_hashables (rai::account const &, rai::block_hash const &, rai::account const &, rai::amount const &, rai::uint256_union const &);
	state_hashables (bool &, rai::stream &);
	state_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	// Account# / public key that operates this account
	// Uses:
	// Bulk signature validation in advance of further ledger processing
	// Arranging uncomitted transactions by account
	rai::account account;
	// Previous transaction in this chain
	rai::block_hash previous;
	// Representative of this account
	rai::account representative;
	// Current balance of this account
	// Allows lookup of account balance simply by looking at the head block
	rai::amount balance;
	// Link field contains source block_hash if receiving, destination account if sending
	rai::uint256_union link;
};
class state_block : public rai::block
{
public:
	state_block (rai::account const &, rai::block_hash const &, rai::account const &, rai::amount const &, rai::uint256_union const &, rai::raw_key const &, rai::public_key const &, uint64_t);
	state_block (bool &, rai::stream &);
	state_block (bool &, boost::property_tree::ptree const &);
	virtual ~state_block () = default;
	using rai::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	rai::block_hash previous () const override;
	rai::block_hash source () const override;
	rai::block_hash root () const override;
	rai::account representative () const override;
	void serialize (rai::stream &) const override;
	void serialize_json (std::string &) const override;
	bool deserialize (rai::stream &);
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (rai::block_visitor &) const override;
	rai::block_type type () const override;
	rai::signature block_signature () const override;
	void signature_set (rai::uint512_union const &) override;
	bool operator== (rai::block const &) const override;
	bool operator== (rai::state_block const &) const;
	bool valid_predecessor (rai::block const &) const override;
	static size_t constexpr size = sizeof (rai::account) + sizeof (rai::block_hash) + sizeof (rai::account) + sizeof (rai::amount) + sizeof (rai::uint256_union) + sizeof (rai::signature) + sizeof (uint64_t);
	rai::state_hashables hashables;
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
	virtual void state_block (rai::state_block const &) = 0;
	virtual ~block_visitor () = default;
};
std::unique_ptr<rai::block> deserialize_block (rai::stream &);
std::unique_ptr<rai::block> deserialize_block (rai::stream &, rai::block_type);
std::unique_ptr<rai::block> deserialize_block_json (boost::property_tree::ptree const &);
void serialize_block (rai::stream &, rai::block const &);
}
