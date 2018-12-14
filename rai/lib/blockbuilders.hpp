#pragma once

#include <memory>
#include <rai/lib/blocks.hpp>
#include <rai/lib/errors.hpp>

namespace rai
{
/** Base type for block builder implementations */
template <typename BLOCKTYPE>
class abstract_builder
{
public:
	/** Returns the built block as a unique_ptr */
	inline std::unique_ptr<BLOCKTYPE> build ()
	{
		assert (!ec);
		return std::move (block);
	}

	/** Returns the built block as a unique_ptr. Any errors are placed in \p ec */
	inline std::unique_ptr<BLOCKTYPE> build (std::error_code & ec)
	{
		ec = this->ec;
		return std::move (block);
	}

	/** Set work value */
	inline abstract_builder & work (uint64_t work)
	{
		block->work = work;
		return *this;
	}

	/** Sign the block using the \p private_key and \p public_key */
	inline abstract_builder & sign (rai::raw_key const & private_key, rai::public_key const & public_key)
	{
		block->signature = rai::sign_message (private_key, public_key, block->hash ());
		return *this;
	}

	/**
	 * Prepares a new block to be built, allowing a builder to be reused.
	 * It is not necessary to call this explicitely if the block_builder#<blocktype>()
	 * functions are called for each new block. However, if the builder returned by e.g.
	 * block_builder#state() is saved in a variable and reused, reset() must be called
	 * to prepare a new block.
	 */
	inline void reset ()
	{
		block = std::make_unique<BLOCKTYPE> ();
		ec.clear ();
	}

protected:
	abstract_builder ()
	{
		reset ();
	}

	/** The block we're building. Clients can convert this to shared_ptr as needed. */
	std::unique_ptr<BLOCKTYPE> block;

	/**
	 * Set if any build functions fail. This will be output via the build_ functions,
	 * or cause an assert for the parameter-less overloads.
	 */
	std::error_code ec;
};

/** Builder for state blocks */
class state_block_builder : public abstract_builder<rai::state_block>
{
public:
	state_block_builder () = default;
	/** Sets all hashables, signature and work to zero. */
	state_block_builder & clear ();
	/** Set account */
	state_block_builder & account (rai::account account);
	/** Set account from hex representation of public key */
	state_block_builder & account_hex (std::string account_hex);
	/** Set account from an xrb_ or nano_ address */
	state_block_builder & account_address (std::string account_address);
	/** Set representative */
	state_block_builder & representative (rai::account account);
	/** Set representative from hex representation of public key */
	state_block_builder & representative_hex (std::string account_hex);
	/** Set representative from an xrb_ or nano_ address */
	state_block_builder & representative_address (std::string account_address);
	/** Set previous block hash */
	state_block_builder & previous (rai::block_hash previous);
	/** Set previous block hash from hex representation */
	state_block_builder & previous_hex (std::string previous_hex);
	/** Set balance */
	state_block_builder & balance (rai::amount balance);
	/** Set balance from decimal string */
	state_block_builder & balance_dec (std::string balance_decimal);
	/** Set balance from hex string */
	state_block_builder & balance_hex (std::string balance_hex);
	/** Set link */
	state_block_builder & link (rai::uint256_union link);
	/** Set link from hex representation */
	state_block_builder & link_hex (std::string link_hex);
	/** Set link from an xrb_ or nano_ address */
	state_block_builder & link_address (std::string link_address);
};

/** Builder for open blocks */
class open_block_builder : public abstract_builder<rai::open_block>
{
public:
	open_block_builder () = default;
	/** Sets all hashables, signature and work to zero. */
	open_block_builder & clear ();
	/** Set account */
	open_block_builder & account (rai::account account);
	/** Set account from hex representation of public key */
	open_block_builder & account_hex (std::string account_hex);
	/** Set account from an xrb_ or nano_ address */
	open_block_builder & account_address (std::string account_address);
	/** Set representative */
	open_block_builder & representative (rai::account account);
	/** Set representative from hex representation of public key */
	open_block_builder & representative_hex (std::string account_hex);
	/** Set representative from an xrb_ or nano_ address */
	open_block_builder & representative_address (std::string account_address);
	/** Set source block hash */
	open_block_builder & source (rai::block_hash source);
	/** Set source block hash from hex representation */
	open_block_builder & source_hex (std::string source_hex);
};

/** Builder for change blocks */
class change_block_builder : public abstract_builder<rai::change_block>
{
public:
	change_block_builder () = default;
	/** Sets all hashables, signature and work to zero. */
	change_block_builder & clear ();
	/** Set representative */
	change_block_builder & representative (rai::account account);
	/** Set representative from hex representation of public key */
	change_block_builder & representative_hex (std::string account_hex);
	/** Set representative from an xrb_ or nano_ address */
	change_block_builder & representative_address (std::string account_address);
	/** Set previous block hash */
	change_block_builder & previous (rai::block_hash previous);
	/** Set previous block hash from hex representation */
	change_block_builder & previous_hex (std::string previous_hex);
};

/** Builder for send blocks */
class send_block_builder : public abstract_builder<rai::send_block>
{
public:
	send_block_builder () = default;
	/** Sets all hashables, signature and work to zero. */
	send_block_builder & clear ();
	/** Set destination */
	send_block_builder & destination (rai::account account);
	/** Set destination from hex representation of public key */
	send_block_builder & destination_hex (std::string account_hex);
	/** Set destination from an xrb_ or nano_ address */
	send_block_builder & destination_address (std::string account_address);
	/** Set previous block hash */
	send_block_builder & previous (rai::block_hash previous);
	/** Set previous block hash from hex representation */
	send_block_builder & previous_hex (std::string previous_hex);
	/** Set balance */
	send_block_builder & balance (rai::amount balance);
	/** Set balance from decimal string */
	send_block_builder & balance_dec (std::string balance_decimal);
	/** Set balance from hex string */
	send_block_builder & balance_hex (std::string balance_hex);
};

/** Builder for receive blocks */
class receive_block_builder : public abstract_builder<rai::receive_block>
{
public:
	receive_block_builder () = default;
	/** Sets all hashables, signature and work to zero. */
	receive_block_builder & clear ();
	/** Set previous block hash */
	receive_block_builder & previous (rai::block_hash previous);
	/** Set previous block hash from hex representation */
	receive_block_builder & previous_hex (std::string previous_hex);
	/** Set source block hash */
	receive_block_builder & source (rai::block_hash source);
	/** Set source block hash from hex representation */
	receive_block_builder & source_hex (std::string source_hex);
};

/** Block builder to simplify construction of the various block types */
class block_builder
{
public:
	/** Prepares a new state block and returns a block builder */
	inline rai::state_block_builder & state ()
	{
		state_builder.reset ();
		return state_builder;
	}

	/** Prepares a new open block and returns a block builder */
	inline rai::open_block_builder & open ()
	{
		open_builder.reset ();
		return open_builder;
	}

	/** Prepares a new change block and returns a block builder */
	inline rai::change_block_builder & change ()
	{
		change_builder.reset ();
		return change_builder;
	}

	/** Prepares a new send block and returns a block builder */
	inline rai::send_block_builder & send ()
	{
		send_builder.reset ();
		return send_builder;
	}

	/** Prepares a new receive block and returns a block builder */
	inline rai::receive_block_builder & receive ()
	{
		receive_builder.reset ();
		return receive_builder;
	}

private:
	rai::state_block_builder state_builder;
	rai::open_block_builder open_builder;
	rai::change_block_builder change_builder;
	rai::send_block_builder send_builder;
	rai::receive_block_builder receive_builder;
};
}
