#pragma once

#include <nano/crypto/blake2/blake2.h>
#include <nano/lib/errors.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/utility.hpp>

#include <boost/property_tree/json_parser.hpp>

#include <cassert>
#include <streambuf>
#include <unordered_map>

namespace nano
{
// We operate on streams of uint8_t by convention
using stream = std::basic_streambuf<uint8_t>;
// Read a raw byte stream the size of `T' and fill value.
template <typename T>
bool try_read (nano::stream & stream_a, T & value)
{
	static_assert (std::is_standard_layout<T>::value, "Can't stream read non-standard layout types");
	auto amount_read (stream_a.sgetn (reinterpret_cast<uint8_t *> (&value), sizeof (value)));
	return amount_read != sizeof (value);
}
// A wrapper of try_read which throws if there is an error
template <typename T>
void read (nano::stream & stream_a, T & value)
{
	auto error = try_read (stream_a, value);
	if (error)
	{
		throw std::runtime_error ("Failed to read type");
	}
}

template <typename T>
void write (nano::stream & stream_a, T const & value)
{
	static_assert (std::is_standard_layout<T>::value, "Can't stream write non-standard layout types");
	auto amount_written (stream_a.sputn (reinterpret_cast<uint8_t const *> (&value), sizeof (value)));
	(void)amount_written;
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
	nano::block_hash hash () const;
	// Return a digest of hashables and non-hashables in this block.
	nano::block_hash full_hash () const;
	std::string to_json () const;
	virtual void hash (blake2b_state &) const = 0;
	virtual uint64_t block_work () const = 0;
	virtual void block_work_set (uint64_t) = 0;
	virtual nano::account account () const;
	// Previous block in account's chain, zero for open block
	virtual nano::block_hash previous () const = 0;
	// Source block for open/receive blocks, zero otherwise.
	virtual nano::block_hash source () const;
	// Previous block or account number for open blocks
	virtual nano::block_hash root () const = 0;
	// Qualified root value based on previous() and root()
	virtual nano::qualified_root qualified_root () const;
	// Link field for state blocks, zero otherwise.
	virtual nano::block_hash link () const;
	virtual nano::account representative () const;
	virtual nano::amount balance () const;
	virtual void serialize (nano::stream &) const = 0;
	virtual void serialize_json (std::string &, bool = false) const = 0;
	virtual void serialize_json (boost::property_tree::ptree &) const = 0;
	virtual void visit (nano::block_visitor &) const = 0;
	virtual bool operator== (nano::block const &) const = 0;
	virtual nano::block_type type () const = 0;
	virtual nano::signature block_signature () const = 0;
	virtual void signature_set (nano::uint512_union const &) = 0;
	virtual ~block () = default;
	virtual bool valid_predecessor (nano::block const &) const = 0;
	static size_t size (nano::block_type);
};
class send_hashables
{
public:
	send_hashables () = default;
	send_hashables (nano::account const &, nano::block_hash const &, nano::amount const &);
	send_hashables (bool &, nano::stream &);
	send_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	nano::block_hash previous;
	nano::account destination;
	nano::amount balance;
	static size_t constexpr size = sizeof (previous) + sizeof (destination) + sizeof (balance);
};
class send_block : public nano::block
{
public:
	send_block () = default;
	send_block (nano::block_hash const &, nano::account const &, nano::amount const &, nano::raw_key const &, nano::public_key const &, uint64_t);
	send_block (bool &, nano::stream &);
	send_block (bool &, boost::property_tree::ptree const &);
	virtual ~send_block () = default;
	using nano::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	nano::block_hash previous () const override;
	nano::block_hash root () const override;
	nano::amount balance () const override;
	void serialize (nano::stream &) const override;
	bool deserialize (nano::stream &);
	void serialize_json (std::string &, bool = false) const override;
	void serialize_json (boost::property_tree::ptree &) const override;
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (nano::block_visitor &) const override;
	nano::block_type type () const override;
	nano::signature block_signature () const override;
	void signature_set (nano::uint512_union const &) override;
	bool operator== (nano::block const &) const override;
	bool operator== (nano::send_block const &) const;
	bool valid_predecessor (nano::block const &) const override;
	send_hashables hashables;
	nano::signature signature;
	uint64_t work;
	static size_t constexpr size = nano::send_hashables::size + sizeof (signature) + sizeof (work);
};
class receive_hashables
{
public:
	receive_hashables () = default;
	receive_hashables (nano::block_hash const &, nano::block_hash const &);
	receive_hashables (bool &, nano::stream &);
	receive_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	nano::block_hash previous;
	nano::block_hash source;
	static size_t constexpr size = sizeof (previous) + sizeof (source);
};
class receive_block : public nano::block
{
public:
	receive_block () = default;
	receive_block (nano::block_hash const &, nano::block_hash const &, nano::raw_key const &, nano::public_key const &, uint64_t);
	receive_block (bool &, nano::stream &);
	receive_block (bool &, boost::property_tree::ptree const &);
	virtual ~receive_block () = default;
	using nano::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	nano::block_hash previous () const override;
	nano::block_hash source () const override;
	nano::block_hash root () const override;
	void serialize (nano::stream &) const override;
	bool deserialize (nano::stream &);
	void serialize_json (std::string &, bool = false) const override;
	void serialize_json (boost::property_tree::ptree &) const override;
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (nano::block_visitor &) const override;
	nano::block_type type () const override;
	nano::signature block_signature () const override;
	void signature_set (nano::uint512_union const &) override;
	bool operator== (nano::block const &) const override;
	bool operator== (nano::receive_block const &) const;
	bool valid_predecessor (nano::block const &) const override;
	receive_hashables hashables;
	nano::signature signature;
	uint64_t work;
	static size_t constexpr size = nano::receive_hashables::size + sizeof (signature) + sizeof (work);
};
class open_hashables
{
public:
	open_hashables () = default;
	open_hashables (nano::block_hash const &, nano::account const &, nano::account const &);
	open_hashables (bool &, nano::stream &);
	open_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	nano::block_hash source;
	nano::account representative;
	nano::account account;
	static size_t constexpr size = sizeof (source) + sizeof (representative) + sizeof (account);
};
class open_block : public nano::block
{
public:
	open_block () = default;
	open_block (nano::block_hash const &, nano::account const &, nano::account const &, nano::raw_key const &, nano::public_key const &, uint64_t);
	open_block (nano::block_hash const &, nano::account const &, nano::account const &, std::nullptr_t);
	open_block (bool &, nano::stream &);
	open_block (bool &, boost::property_tree::ptree const &);
	virtual ~open_block () = default;
	using nano::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	nano::block_hash previous () const override;
	nano::account account () const override;
	nano::block_hash source () const override;
	nano::block_hash root () const override;
	nano::account representative () const override;
	void serialize (nano::stream &) const override;
	bool deserialize (nano::stream &);
	void serialize_json (std::string &, bool = false) const override;
	void serialize_json (boost::property_tree::ptree &) const override;
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (nano::block_visitor &) const override;
	nano::block_type type () const override;
	nano::signature block_signature () const override;
	void signature_set (nano::uint512_union const &) override;
	bool operator== (nano::block const &) const override;
	bool operator== (nano::open_block const &) const;
	bool valid_predecessor (nano::block const &) const override;
	nano::open_hashables hashables;
	nano::signature signature;
	uint64_t work;
	static size_t constexpr size = nano::open_hashables::size + sizeof (signature) + sizeof (work);
};
class change_hashables
{
public:
	change_hashables () = default;
	change_hashables (nano::block_hash const &, nano::account const &);
	change_hashables (bool &, nano::stream &);
	change_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	nano::block_hash previous;
	nano::account representative;
	static size_t constexpr size = sizeof (previous) + sizeof (representative);
};
class change_block : public nano::block
{
public:
	change_block () = default;
	change_block (nano::block_hash const &, nano::account const &, nano::raw_key const &, nano::public_key const &, uint64_t);
	change_block (bool &, nano::stream &);
	change_block (bool &, boost::property_tree::ptree const &);
	virtual ~change_block () = default;
	using nano::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	nano::block_hash previous () const override;
	nano::block_hash root () const override;
	nano::account representative () const override;
	void serialize (nano::stream &) const override;
	bool deserialize (nano::stream &);
	void serialize_json (std::string &, bool = false) const override;
	void serialize_json (boost::property_tree::ptree &) const override;
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (nano::block_visitor &) const override;
	nano::block_type type () const override;
	nano::signature block_signature () const override;
	void signature_set (nano::uint512_union const &) override;
	bool operator== (nano::block const &) const override;
	bool operator== (nano::change_block const &) const;
	bool valid_predecessor (nano::block const &) const override;
	nano::change_hashables hashables;
	nano::signature signature;
	uint64_t work;
	static size_t constexpr size = nano::change_hashables::size + sizeof (signature) + sizeof (work);
};
class state_hashables
{
public:
	state_hashables () = default;
	state_hashables (nano::account const &, nano::block_hash const &, nano::account const &, nano::amount const &, nano::uint256_union const &);
	state_hashables (bool &, nano::stream &);
	state_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	// Account# / public key that operates this account
	// Uses:
	// Bulk signature validation in advance of further ledger processing
	// Arranging uncomitted transactions by account
	nano::account account;
	// Previous transaction in this chain
	nano::block_hash previous;
	// Representative of this account
	nano::account representative;
	// Current balance of this account
	// Allows lookup of account balance simply by looking at the head block
	nano::amount balance;
	// Link field contains source block_hash if receiving, destination account if sending
	nano::uint256_union link;
	// Serialized size
	static size_t constexpr size = sizeof (account) + sizeof (previous) + sizeof (representative) + sizeof (balance) + sizeof (link);
};
class state_block : public nano::block
{
public:
	state_block () = default;
	state_block (nano::account const &, nano::block_hash const &, nano::account const &, nano::amount const &, nano::uint256_union const &, nano::raw_key const &, nano::public_key const &, uint64_t);
	state_block (bool &, nano::stream &);
	state_block (bool &, boost::property_tree::ptree const &);
	virtual ~state_block () = default;
	using nano::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	nano::block_hash previous () const override;
	nano::account account () const override;
	nano::block_hash root () const override;
	nano::block_hash link () const override;
	nano::account representative () const override;
	nano::amount balance () const override;
	void serialize (nano::stream &) const override;
	bool deserialize (nano::stream &);
	void serialize_json (std::string &, bool = false) const override;
	void serialize_json (boost::property_tree::ptree &) const override;
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (nano::block_visitor &) const override;
	nano::block_type type () const override;
	nano::signature block_signature () const override;
	void signature_set (nano::uint512_union const &) override;
	bool operator== (nano::block const &) const override;
	bool operator== (nano::state_block const &) const;
	bool valid_predecessor (nano::block const &) const override;
	nano::state_hashables hashables;
	nano::signature signature;
	uint64_t work;
	static size_t constexpr size = nano::state_hashables::size + sizeof (signature) + sizeof (work);
};
class block_visitor
{
public:
	virtual void send_block (nano::send_block const &) = 0;
	virtual void receive_block (nano::receive_block const &) = 0;
	virtual void open_block (nano::open_block const &) = 0;
	virtual void change_block (nano::change_block const &) = 0;
	virtual void state_block (nano::state_block const &) = 0;
	virtual ~block_visitor () = default;
};
/**
 * This class serves to find and return unique variants of a block in order to minimize memory usage
 */
class block_uniquer
{
public:
	using value_type = std::pair<const nano::uint256_union, std::weak_ptr<nano::block>>;

	std::shared_ptr<nano::block> unique (std::shared_ptr<nano::block>);
	size_t size ();

private:
	std::mutex mutex;
	std::unordered_map<std::remove_const_t<value_type::first_type>, value_type::second_type> blocks;
	static unsigned constexpr cleanup_count = 2;
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (block_uniquer & block_uniquer, const std::string & name);

std::shared_ptr<nano::block> deserialize_block (nano::stream &);
std::shared_ptr<nano::block> deserialize_block (nano::stream &, nano::block_type, nano::block_uniquer * = nullptr);
std::shared_ptr<nano::block> deserialize_block_json (boost::property_tree::ptree const &, nano::block_uniquer * = nullptr);
void serialize_block (nano::stream &, nano::block const &);
void block_memory_pool_purge ();
}
