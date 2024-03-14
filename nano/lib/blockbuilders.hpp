#pragma once

#include <nano/lib/numbers.hpp>

#include <memory>

namespace nano
{
class change_block;
class send_block;
class state_block;
class open_block;
class receive_block;
}

namespace nano
{
/** Flags to track builder state */
enum class build_flags : uint8_t
{
	signature_present = 1,
	work_present = 2,
	account_present = 4,
	balance_present = 8,
	/* link also covers source and destination for legacy blocks */
	link_present = 16,
	previous_present = 32,
	representative_present = 64
};

inline nano::build_flags operator| (nano::build_flags a, nano::build_flags b)
{
	return static_cast<nano::build_flags> (static_cast<uint8_t> (a) | static_cast<uint8_t> (b));
}
inline uint8_t operator| (uint8_t a, nano::build_flags b)
{
	return static_cast<uint8_t> (a | static_cast<uint8_t> (b));
}
inline uint8_t operator& (uint8_t a, nano::build_flags b)
{
	return static_cast<uint8_t> (a & static_cast<uint8_t> (b));
}
inline uint8_t operator|= (uint8_t & a, nano::build_flags b)
{
	return a = static_cast<uint8_t> (a | static_cast<uint8_t> (b));
}

/**
 * Base type for block builder implementations. We employ static polymorphism
 * to pass validation through subtypes without incurring the vtable cost.
 */
template <typename BLOCKTYPE, typename BUILDER>
class abstract_builder
{
public:
	/** Returns the built block*/
	std::shared_ptr<BLOCKTYPE> build ();
	/** Returns the built block. Any errors are placed in \p ec */
	std::shared_ptr<BLOCKTYPE> build (std::error_code & ec);
	/** Set work value */
	abstract_builder & work (uint64_t work);
	/** Sign the block using the \p private_key and \p public_key */
	abstract_builder & sign (nano::raw_key const & private_key, nano::public_key const & public_key);
	/** Set signature to zero to pass build() validation, allowing block to be signed at a later point. This is mostly useful for tests. */
	abstract_builder & sign_zero ();

protected:
	abstract_builder () = default;

	/** Create a new block and resets the internal builder state */
	void construct_block ();

	/** The block we're building. Clients can convert this to shared_ptr as needed. */
	std::unique_ptr<BLOCKTYPE> block;

	/**
	 * Set if any builder functions fail. This will be output via the build(std::error_code) function,
	 * or cause an assert for the parameter-less overload.
	 */
	std::error_code ec;

	/** Bitset to track build state */
	uint8_t build_state{ 0 };

	/** Required field shared by all block types*/
	uint8_t base_fields = static_cast<uint8_t> (nano::build_flags::work_present | nano::build_flags::signature_present);
};

/** Builder for state blocks */
class state_block_builder : public abstract_builder<nano::state_block, state_block_builder>
{
public:
	/** Creates a state block builder by calling make_block() */
	state_block_builder ();
	/** Initialize from an existing block */
	state_block_builder & from (nano::state_block const & block);
	/** Creates a new block with fields, signature and work set to sentinel values. All fields must be set or zeroed for build() to succeed. */
	state_block_builder & make_block ();
	/** Sets all hashables, signature and work to zero. */
	state_block_builder & zero ();
	/** Set account */
	state_block_builder & account (nano::account const & account);
	/** Set account from hex representation of public key */
	state_block_builder & account_hex (std::string const & account_hex);
	/** Set account from an xrb_ or nano_ address */
	state_block_builder & account_address (std::string const & account_address);
	/** Set representative */
	state_block_builder & representative (nano::account const & account);
	/** Set representative from hex representation of public key */
	state_block_builder & representative_hex (std::string const & account_hex);
	/** Set representative from an xrb_ or nano_ address */
	state_block_builder & representative_address (std::string const & account_address);
	/** Set previous block hash */
	state_block_builder & previous (nano::block_hash const & previous);
	/** Set previous block hash from hex representation */
	state_block_builder & previous_hex (std::string const & previous_hex);
	/** Set balance */
	state_block_builder & balance (nano::amount const & balance);
	/** Set balance from decimal string */
	state_block_builder & balance_dec (std::string const & balance_decimal);
	/** Set balance from hex string */
	state_block_builder & balance_hex (std::string const & balance_hex);
	/** Set link */
	state_block_builder & link (nano::link const & link);
	/** Set link from hex representation */
	state_block_builder & link_hex (std::string const & link_hex);
	/** Set link from an xrb_ or nano_ address */
	state_block_builder & link_address (std::string const & link_address);
	/** Provides validation for build() */
	void validate ();

private:
	uint8_t required_fields = base_fields | static_cast<uint8_t> (nano::build_flags::account_present | nano::build_flags::balance_present | nano::build_flags::link_present | nano::build_flags::previous_present | nano::build_flags::representative_present);
};

/** Builder for open blocks */
class open_block_builder : public abstract_builder<nano::open_block, open_block_builder>
{
public:
	/** Creates an open block builder by calling make_block() */
	open_block_builder ();
	/** Creates a new block with fields, signature and work set to sentinel values. All fields must be set or zeroed for build() to succeed. */
	open_block_builder & make_block ();
	/** Sets all hashables, signature and work to zero. */
	open_block_builder & zero ();
	/** Set account */
	open_block_builder & account (nano::account account);
	/** Set account from hex representation of public key */
	open_block_builder & account_hex (std::string account_hex);
	/** Set account from an xrb_ or nano_ address */
	open_block_builder & account_address (std::string account_address);
	/** Set representative */
	open_block_builder & representative (nano::account account);
	/** Set representative from hex representation of public key */
	open_block_builder & representative_hex (std::string account_hex);
	/** Set representative from an xrb_ or nano_ address */
	open_block_builder & representative_address (std::string account_address);
	/** Set source block hash */
	open_block_builder & source (nano::block_hash source);
	/** Set source block hash from hex representation */
	open_block_builder & source_hex (std::string source_hex);
	/** Provides validation for build() */
	void validate ();

private:
	uint8_t required_fields = base_fields | static_cast<uint8_t> (nano::build_flags::account_present | nano::build_flags::representative_present | nano::build_flags::link_present);
};

/** Builder for change blocks */
class change_block_builder : public abstract_builder<nano::change_block, change_block_builder>
{
public:
	/** Create a change block builder by calling make_block() */
	change_block_builder ();
	/** Creates a new block with fields, signature and work set to sentinel values. All fields must be set or zeroed for build() to succeed. */
	change_block_builder & make_block ();
	/** Sets all hashables, signature and work to zero. */
	change_block_builder & zero ();
	/** Set representative */
	change_block_builder & representative (nano::account account);
	/** Set representative from hex representation of public key */
	change_block_builder & representative_hex (std::string account_hex);
	/** Set representative from an xrb_ or nano_ address */
	change_block_builder & representative_address (std::string account_address);
	/** Set previous block hash */
	change_block_builder & previous (nano::block_hash previous);
	/** Set previous block hash from hex representation */
	change_block_builder & previous_hex (std::string previous_hex);
	/** Provides validation for build() */
	void validate ();

private:
	uint8_t required_fields = base_fields | static_cast<uint8_t> (nano::build_flags::previous_present | nano::build_flags::representative_present);
};

/** Builder for send blocks */
class send_block_builder : public abstract_builder<nano::send_block, send_block_builder>
{
public:
	/** Creates a send block builder by calling make_block() */
	send_block_builder ();
	/** Creates a new block with fields, signature and work set to sentinel values. All fields must be set or zeroed for build() to succeed. */
	send_block_builder & make_block ();
	/** Sets all hashables, signature and work to zero. */
	send_block_builder & zero ();
	/** Set destination */
	send_block_builder & destination (nano::account account);
	/** Set destination from hex representation of public key */
	send_block_builder & destination_hex (std::string account_hex);
	/** Set destination from an xrb_ or nano_ address */
	send_block_builder & destination_address (std::string account_address);
	/** Set previous block hash */
	send_block_builder & previous (nano::block_hash previous);
	/** Set previous block hash from hex representation */
	send_block_builder & previous_hex (std::string previous_hex);
	/** Set balance */
	send_block_builder & balance (nano::amount balance);
	/** Set balance from decimal string */
	send_block_builder & balance_dec (std::string balance_decimal);
	/** Set balance from hex string */
	send_block_builder & balance_hex (std::string balance_hex);
	/** Provides validation for build() */
	void validate ();

private:
	uint8_t required_fields = base_fields | static_cast<uint8_t> (build_flags::previous_present | build_flags::link_present | build_flags::balance_present);
};

/** Builder for receive blocks */
class receive_block_builder : public abstract_builder<nano::receive_block, receive_block_builder>
{
public:
	/** Creates a receive block by calling make_block() */
	receive_block_builder ();
	/** Creates a new block with fields, signature and work set to sentinel values. All fields must be set or zeroed for build() to succeed. */
	receive_block_builder & make_block ();
	/** Sets all hashables, signature and work to zero. */
	receive_block_builder & zero ();
	/** Set previous block hash */
	receive_block_builder & previous (nano::block_hash previous);
	/** Set previous block hash from hex representation */
	receive_block_builder & previous_hex (std::string previous_hex);
	/** Set source block hash */
	receive_block_builder & source (nano::block_hash source);
	/** Set source block hash from hex representation */
	receive_block_builder & source_hex (std::string source_hex);
	/** Provides validation for build() */
	void validate ();

private:
	uint8_t required_fields = base_fields | static_cast<uint8_t> (build_flags::previous_present | build_flags::link_present);
};

/** Block builder to simplify construction of the various block types */
class block_builder
{
public:
	/** Prepares a new state block and returns a block builder */
	nano::state_block_builder & state ()
	{
		state_builder.make_block ();
		return state_builder;
	}

	/** Prepares a new open block and returns a block builder */
	nano::open_block_builder & open ()
	{
		open_builder.make_block ();
		return open_builder;
	}

	/** Prepares a new change block and returns a block builder */
	nano::change_block_builder & change ()
	{
		change_builder.make_block ();
		return change_builder;
	}

	/** Prepares a new send block and returns a block builder */
	nano::send_block_builder & send ()
	{
		send_builder.make_block ();
		return send_builder;
	}

	/** Prepares a new receive block and returns a block builder */
	nano::receive_block_builder & receive ()
	{
		receive_builder.make_block ();
		return receive_builder;
	}

private:
	nano::state_block_builder state_builder;
	nano::open_block_builder open_builder;
	nano::change_block_builder change_builder;
	nano::send_block_builder send_builder;
	nano::receive_block_builder receive_builder;
};
}
