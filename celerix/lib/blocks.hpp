#pragma once

#include <celerix/lib/block_sideband.hpp>
#include <celerix/lib/epoch.hpp>
#include <celerix/lib/errors.hpp>
#include <celerix/lib/fwd.hpp>
#include <celerix/lib/numbers.hpp>
#include <celerix/lib/optional_ptr.hpp>

#include <boost/property_tree/ptree_fwd.hpp>

#include <optional>

typedef struct blake2b_state__ blake2b_state;

namespace celerix
{
using block_uniquer = uniquer<celerix::uint256_union, celerix::block>;
}

namespace celerix
{
class block
{
public:
	virtual ~block () = default;
	// Return a digest of the hashables in this block.
	celerix::block_hash const & hash () const;
	// Return a digest of hashables and non-hashables in this block.
	celerix::block_hash full_hash () const;
	celerix::block_sideband const & sideband () const;
	void sideband_set (celerix::block_sideband const &);
	bool has_sideband () const;
	std::string to_json () const;
	virtual uint64_t block_work () const = 0;
	virtual void block_work_set (uint64_t) = 0;
	// Previous block or account number for open blocks
	virtual celerix::root root () const = 0;
	// Qualified root value based on previous() and root()
	virtual celerix::qualified_root qualified_root () const;
	virtual void serialize (celerix::stream &) const = 0;
	virtual void serialize_json (std::string &, bool = false) const = 0;
	virtual void serialize_json (boost::property_tree::ptree &) const = 0;
	virtual void visit (celerix::block_visitor &) const = 0;
	virtual void visit (celerix::mutable_block_visitor &) = 0;
	virtual bool operator== (celerix::block const &) const = 0;
	virtual celerix::block_type type () const = 0;
	virtual celerix::signature const & block_signature () const = 0;
	virtual void signature_set (celerix::signature const &) = 0;
	virtual bool valid_predecessor (celerix::block const &) const = 0;
	static size_t size (celerix::block_type);
	virtual celerix::work_version work_version () const;
	virtual std::shared_ptr<celerix::block> clone () const = 0;
	// If there are any changes to the hashables, call this to update the cached hash
	void refresh ();
	bool is_send () const noexcept;
	bool is_receive () const noexcept;
	bool is_change () const noexcept;
	bool is_epoch () const noexcept;

public: // Direct access to the block fields or nullopt if the block type does not have the specified field
	// Returns account field or account from sideband
	celerix::account account () const noexcept;
	// Account field for open/state blocks
	virtual std::optional<celerix::account> account_field () const;
	// Returns the balance field or balance from sideband
	celerix::amount balance () const noexcept;
	// Balance field for open/send/state blocks
	virtual std::optional<celerix::amount> balance_field () const;
	// Returns the destination account for send/state blocks that are sends
	celerix::account destination () const noexcept;
	// Destination account for send blocks
	virtual std::optional<celerix::account> destination_field () const;
	// Link field for state blocks
	virtual std::optional<celerix::link> link_field () const;
	// Previous block if field exists or 0
	celerix::block_hash previous () const noexcept;
	// Previous block in chain if the field exists
	virtual std::optional<celerix::block_hash> previous_field () const = 0;
	// Representative field for open/change blocks
	virtual std::optional<celerix::account> representative_field () const;
	// Returns the source block hash for open/receive/state blocks that are receives
	celerix::block_hash source () const noexcept;
	// Source block for open/receive blocks
	virtual std::optional<celerix::block_hash> source_field () const;

protected:
	virtual void generate_hash (blake2b_state &) const = 0;
	mutable celerix::block_hash cached_hash{ 0 };
	/**
	 * Contextual details about a block, some fields may or may not be set depending on block type.
	 * This field is set via sideband_set in ledger processing or deserializing blocks from the database.
	 * Otherwise it may be null (for example, an old block or fork).
	 */
	celerix::optional_ptr<celerix::block_sideband> sideband_m;

private:
	celerix::block_hash generate_hash () const;

public: // Logging
	virtual void operator() (celerix::object_stream &) const;
};

class send_hashables
{
public:
	send_hashables () = default;
	send_hashables (celerix::block_hash const &, celerix::account const &, celerix::amount const &);
	send_hashables (bool &, celerix::stream &);
	send_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	celerix::block_hash previous;
	celerix::account destination;
	celerix::amount balance;
	static std::size_t constexpr size = sizeof (previous) + sizeof (destination) + sizeof (balance);
};

class send_block : public celerix::block
{
public:
	send_block () = default;
	send_block (celerix::block_hash const &, celerix::account const &, celerix::amount const &, celerix::raw_key const &, celerix::public_key const &, uint64_t);
	send_block (bool &, celerix::stream &);
	send_block (bool &, boost::property_tree::ptree const &);
	virtual ~send_block () = default;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	celerix::root root () const override;
	void serialize (celerix::stream &) const override;
	bool deserialize (celerix::stream &);
	void serialize_json (std::string &, bool = false) const override;
	void serialize_json (boost::property_tree::ptree &) const override;
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (celerix::block_visitor &) const override;
	void visit (celerix::mutable_block_visitor &) override;
	celerix::block_type type () const override;
	celerix::signature const & block_signature () const override;
	void signature_set (celerix::signature const &) override;
	bool operator== (celerix::block const &) const override;
	bool operator== (celerix::send_block const &) const;
	bool valid_predecessor (celerix::block const &) const override;
	std::shared_ptr<celerix::block> clone () const override;
	send_hashables hashables;
	celerix::signature signature;
	uint64_t work;
	static std::size_t constexpr size = celerix::send_hashables::size + sizeof (signature) + sizeof (work);

public: // Send block fields
	std::optional<celerix::amount> balance_field () const override;
	std::optional<celerix::account> destination_field () const override;
	std::optional<celerix::block_hash> previous_field () const override;

public: // Logging
	void operator() (celerix::object_stream &) const override;

protected:
	void generate_hash (blake2b_state &) const override;
};

class receive_hashables
{
public:
	receive_hashables () = default;
	receive_hashables (celerix::block_hash const &, celerix::block_hash const &);
	receive_hashables (bool &, celerix::stream &);
	receive_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	celerix::block_hash previous;
	celerix::block_hash source;
	static std::size_t constexpr size = sizeof (previous) + sizeof (source);
};

class receive_block : public celerix::block
{
public:
	receive_block () = default;
	receive_block (celerix::block_hash const &, celerix::block_hash const &, celerix::raw_key const &, celerix::public_key const &, uint64_t);
	receive_block (bool &, celerix::stream &);
	receive_block (bool &, boost::property_tree::ptree const &);
	virtual ~receive_block () = default;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	celerix::root root () const override;
	void serialize (celerix::stream &) const override;
	bool deserialize (celerix::stream &);
	void serialize_json (std::string &, bool = false) const override;
	void serialize_json (boost::property_tree::ptree &) const override;
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (celerix::block_visitor &) const override;
	void visit (celerix::mutable_block_visitor &) override;
	celerix::block_type type () const override;
	celerix::signature const & block_signature () const override;
	void signature_set (celerix::signature const &) override;
	bool operator== (celerix::block const &) const override;
	bool operator== (celerix::receive_block const &) const;
	bool valid_predecessor (celerix::block const &) const override;
	std::shared_ptr<celerix::block> clone () const override;
	receive_hashables hashables;
	celerix::signature signature;
	uint64_t work;
	static std::size_t constexpr size = celerix::receive_hashables::size + sizeof (signature) + sizeof (work);

public: // Receive block fields
	std::optional<celerix::block_hash> previous_field () const override;
	std::optional<celerix::block_hash> source_field () const override;

public: // Logging
	void operator() (celerix::object_stream &) const override;

protected:
	void generate_hash (blake2b_state &) const override;
};

class open_hashables
{
public:
	open_hashables () = default;
	open_hashables (celerix::block_hash const &, celerix::account const &, celerix::account const &);
	open_hashables (bool &, celerix::stream &);
	open_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	celerix::block_hash source;
	celerix::account representative;
	celerix::account account;
	static std::size_t constexpr size = sizeof (source) + sizeof (representative) + sizeof (account);
};

class open_block : public celerix::block
{
public:
	open_block () = default;
	open_block (celerix::block_hash const &, celerix::account const &, celerix::account const &, celerix::raw_key const &, celerix::public_key const &, uint64_t);
	open_block (celerix::block_hash const &, celerix::account const &, celerix::account const &, std::nullptr_t);
	open_block (bool &, celerix::stream &);
	open_block (bool &, boost::property_tree::ptree const &);
	virtual ~open_block () = default;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	celerix::root root () const override;
	void serialize (celerix::stream &) const override;
	bool deserialize (celerix::stream &);
	void serialize_json (std::string &, bool = false) const override;
	void serialize_json (boost::property_tree::ptree &) const override;
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (celerix::block_visitor &) const override;
	void visit (celerix::mutable_block_visitor &) override;
	celerix::block_type type () const override;
	celerix::signature const & block_signature () const override;
	void signature_set (celerix::signature const &) override;
	bool operator== (celerix::block const &) const override;
	bool operator== (celerix::open_block const &) const;
	bool valid_predecessor (celerix::block const &) const override;
	std::shared_ptr<celerix::block> clone () const override;
	celerix::open_hashables hashables;
	celerix::signature signature;
	uint64_t work;
	static std::size_t constexpr size = celerix::open_hashables::size + sizeof (signature) + sizeof (work);

public: // Open block fields
	std::optional<celerix::account> account_field () const override;
	std::optional<celerix::block_hash> previous_field () const override;
	std::optional<celerix::account> representative_field () const override;
	std::optional<celerix::block_hash> source_field () const override;

public: // Logging
	void operator() (celerix::object_stream &) const override;

protected:
	void generate_hash (blake2b_state &) const override;
};

class change_hashables
{
public:
	change_hashables () = default;
	change_hashables (celerix::block_hash const &, celerix::account const &);
	change_hashables (bool &, celerix::stream &);
	change_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	celerix::block_hash previous;
	celerix::account representative;
	static std::size_t constexpr size = sizeof (previous) + sizeof (representative);
};

class change_block : public celerix::block
{
public:
	change_block () = default;
	change_block (celerix::block_hash const &, celerix::account const &, celerix::raw_key const &, celerix::public_key const &, uint64_t);
	change_block (bool &, celerix::stream &);
	change_block (bool &, boost::property_tree::ptree const &);
	virtual ~change_block () = default;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	celerix::root root () const override;
	void serialize (celerix::stream &) const override;
	bool deserialize (celerix::stream &);
	void serialize_json (std::string &, bool = false) const override;
	void serialize_json (boost::property_tree::ptree &) const override;
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (celerix::block_visitor &) const override;
	void visit (celerix::mutable_block_visitor &) override;
	celerix::block_type type () const override;
	celerix::signature const & block_signature () const override;
	void signature_set (celerix::signature const &) override;
	bool operator== (celerix::block const &) const override;
	bool operator== (celerix::change_block const &) const;
	bool valid_predecessor (celerix::block const &) const override;
	std::shared_ptr<celerix::block> clone () const override;
	celerix::change_hashables hashables;
	celerix::signature signature;
	uint64_t work;
	static std::size_t constexpr size = celerix::change_hashables::size + sizeof (signature) + sizeof (work);

public: // Change block fields
	std::optional<celerix::block_hash> previous_field () const override;
	std::optional<celerix::account> representative_field () const override;

public: // Logging
	void operator() (celerix::object_stream &) const override;

protected:
	void generate_hash (blake2b_state &) const override;
};

class state_hashables
{
public:
	state_hashables () = default;
	state_hashables (celerix::account const &, celerix::block_hash const &, celerix::account const &, celerix::amount const &, celerix::link const &);
	state_hashables (bool &, celerix::stream &);
	state_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	// Account# / public key that operates this account
	// Uses:
	// Bulk signature validation in advance of further ledger processing
	// Arranging uncomitted transactions by account
	celerix::account account;
	// Previous transaction in this chain
	celerix::block_hash previous;
	// Representative of this account
	celerix::account representative;
	// Current balance of this account
	// Allows lookup of account balance simply by looking at the head block
	celerix::amount balance;
	// Link field contains source block_hash if receiving, destination account if sending
	celerix::link link;
	// Serialized size
	static std::size_t constexpr size = sizeof (account) + sizeof (previous) + sizeof (representative) + sizeof (balance) + sizeof (link);
};

class state_block : public celerix::block
{
public:
	state_block () = default;
	state_block (celerix::account const &, celerix::block_hash const &, celerix::account const &, celerix::amount const &, celerix::link const &, celerix::raw_key const &, celerix::public_key const &, uint64_t);
	state_block (bool &, celerix::stream &);
	state_block (bool &, boost::property_tree::ptree const &);
	virtual ~state_block () = default;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	celerix::root root () const override;
	void serialize (celerix::stream &) const override;
	bool deserialize (celerix::stream &);
	void serialize_json (std::string &, bool = false) const override;
	void serialize_json (boost::property_tree::ptree &) const override;
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (celerix::block_visitor &) const override;
	void visit (celerix::mutable_block_visitor &) override;
	celerix::block_type type () const override;
	celerix::signature const & block_signature () const override;
	void signature_set (celerix::signature const &) override;
	bool operator== (celerix::block const &) const override;
	bool operator== (celerix::state_block const &) const;
	bool valid_predecessor (celerix::block const &) const override;
	std::shared_ptr<celerix::block> clone () const override;
	celerix::state_hashables hashables;
	celerix::signature signature;
	uint64_t work;
	static std::size_t constexpr size = celerix::state_hashables::size + sizeof (signature) + sizeof (work);

public: // State block fields
	std::optional<celerix::account> account_field () const override;
	std::optional<celerix::amount> balance_field () const override;
	std::optional<celerix::link> link_field () const override;
	std::optional<celerix::block_hash> previous_field () const override;
	std::optional<celerix::account> representative_field () const override;

public: // Logging
	void operator() (celerix::object_stream &) const override;

protected:
	void generate_hash (blake2b_state &) const override;
};

class block_visitor
{
public:
	virtual void send_block (celerix::send_block const &) = 0;
	virtual void receive_block (celerix::receive_block const &) = 0;
	virtual void open_block (celerix::open_block const &) = 0;
	virtual void change_block (celerix::change_block const &) = 0;
	virtual void state_block (celerix::state_block const &) = 0;
	virtual ~block_visitor () = default;
};
class mutable_block_visitor
{
public:
	virtual void send_block (celerix::send_block &) = 0;
	virtual void receive_block (celerix::receive_block &) = 0;
	virtual void open_block (celerix::open_block &) = 0;
	virtual void change_block (celerix::change_block &) = 0;
	virtual void state_block (celerix::state_block &) = 0;
	virtual ~mutable_block_visitor () = default;
};

std::shared_ptr<celerix::block> deserialize_block (celerix::stream &);
std::shared_ptr<celerix::block> deserialize_block (celerix::stream &, celerix::block_type, celerix::block_uniquer * = nullptr);
std::shared_ptr<celerix::block> deserialize_block_json (boost::property_tree::ptree const &, celerix::block_uniquer * = nullptr);
/**
 * Serialize a block prefixed with an 8-bit typecode
 */
void serialize_block (celerix::stream &, celerix::block const &);

void block_memory_pool_purge ();
}
