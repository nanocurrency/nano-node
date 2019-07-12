#pragma once

#include <nano/lib/config.hpp>
#include <nano/lib/memory.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/versioning.hpp>

#include <boost/endian/conversion.hpp>
#include <boost/polymorphic_cast.hpp>

#include <stack>

namespace nano
{
// Generic container to be used when templated db types cannot
class DB_val
{
public:
	DB_val () = default;
	DB_val (size_t size_a, void * data_a) :
	size (size_a),
	data (data_a)
	{
	}

	size_t size;
	void * data;
};

/**
 * Encapsulates database specific container and provides uint256_union conversion of the data.
 */
template <typename Val>
class db_val
{
public:
	db_val (nano::epoch epoch_a = nano::epoch::unspecified) :
	value ({ 0, nullptr }),
	epoch (epoch_a)
	{
	}

	db_val (Val const & value_a, nano::epoch epoch_a = nano::epoch::unspecified) :
	value (value_a),
	epoch (epoch_a)
	{
	}

	db_val (DB_val const & value_a, nano::epoch epoch_a = nano::epoch::unspecified);

	db_val (size_t size_a, void * data_a) :
	value ({ size_a, data_a })
	{
	}

	db_val (nano::uint128_union const & val_a) :
	db_val (sizeof (val_a), const_cast<nano::uint128_union *> (&val_a))
	{
	}

	db_val (nano::uint256_union const & val_a) :
	db_val (sizeof (val_a), const_cast<nano::uint256_union *> (&val_a))
	{
	}

	db_val (nano::account_info const & val_a) :
	db_val (val_a.db_size (), const_cast<nano::account_info *> (&val_a))
	{
	}

	db_val (nano::account_info_v13 const & val_a) :
	db_val (val_a.db_size (), const_cast<nano::account_info_v13 *> (&val_a))
	{
	}

	db_val (nano::pending_info const & val_a) :
	db_val (sizeof (val_a.source) + sizeof (val_a.amount), const_cast<nano::pending_info *> (&val_a))
	{
		static_assert (std::is_standard_layout<nano::pending_info>::value, "Standard layout is required");
	}

	db_val (nano::pending_key const & val_a) :
	db_val (sizeof (val_a), const_cast<nano::pending_key *> (&val_a))
	{
		static_assert (std::is_standard_layout<nano::pending_key>::value, "Standard layout is required");
	}

	db_val (nano::unchecked_info const & val_a) :
	buffer (std::make_shared<std::vector<uint8_t>> ())
	{
		{
			nano::vectorstream stream (*buffer);
			val_a.serialize (stream);
		}
		value = { buffer->size (), const_cast<uint8_t *> (buffer->data ()) };
	}

	db_val (nano::block_info const & val_a) :
	db_val (sizeof (val_a), const_cast<nano::block_info *> (&val_a))
	{
		static_assert (std::is_standard_layout<nano::block_info>::value, "Standard layout is required");
	}

	db_val (nano::endpoint_key const & val_a) :
	db_val (sizeof (val_a), const_cast<nano::endpoint_key *> (&val_a))
	{
		static_assert (std::is_standard_layout<nano::endpoint_key>::value, "Standard layout is required");
	}

	db_val (std::shared_ptr<nano::block> const & val_a) :
	buffer (std::make_shared<std::vector<uint8_t>> ())
	{
		{
			nano::vectorstream stream (*buffer);
			nano::serialize_block (stream, *val_a);
		}
		value = { buffer->size (), const_cast<uint8_t *> (buffer->data ()) };
	}

	db_val (uint64_t val_a) :
	buffer (std::make_shared<std::vector<uint8_t>> ())
	{
		{
			boost::endian::native_to_big_inplace (val_a);
			nano::vectorstream stream (*buffer);
			nano::write (stream, val_a);
		}
		value = { buffer->size (), const_cast<uint8_t *> (buffer->data ()) };
	}

	explicit operator nano::account_info () const
	{
		nano::account_info result;
		result.epoch = epoch;
		assert (size () == result.db_size ());
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + result.db_size (), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	explicit operator nano::account_info_v13 () const
	{
		nano::account_info_v13 result;
		result.epoch = epoch;
		assert (size () == result.db_size ());
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + result.db_size (), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	explicit operator nano::block_info () const
	{
		nano::block_info result;
		assert (size () == sizeof (result));
		static_assert (sizeof (nano::block_info::account) + sizeof (nano::block_info::balance) == sizeof (result), "Packed class");
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (result), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	explicit operator nano::pending_info () const
	{
		nano::pending_info result;
		result.epoch = epoch;
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (nano::pending_info::source) + sizeof (nano::pending_info::amount), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	explicit operator nano::pending_key () const
	{
		nano::pending_key result;
		assert (size () == sizeof (result));
		static_assert (sizeof (nano::pending_key::account) + sizeof (nano::pending_key::hash) == sizeof (result), "Packed class");
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (result), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	explicit operator nano::unchecked_info () const
	{
		nano::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		nano::unchecked_info result;
		bool error (result.deserialize (stream));
		assert (!error);
		return result;
	}

	explicit operator nano::uint128_union () const
	{
		nano::uint128_union result;
		assert (size () == sizeof (result));
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (result), result.bytes.data ());
		return result;
	}

	explicit operator nano::uint256_union () const
	{
		nano::uint256_union result;
		assert (size () == sizeof (result));
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (result), result.bytes.data ());
		return result;
	}

	explicit operator std::array<char, 64> () const
	{
		nano::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		std::array<char, 64> result;
		auto error = nano::try_read (stream, result);
		assert (!error);
		return result;
	}

	explicit operator nano::endpoint_key () const
	{
		nano::endpoint_key result;
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (result), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	explicit operator nano::no_value () const
	{
		return no_value::dummy;
	}

	explicit operator std::shared_ptr<nano::block> () const
	{
		nano::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		std::shared_ptr<nano::block> result (nano::deserialize_block (stream));
		return result;
	}

	template <typename Block>
	std::shared_ptr<Block> convert_to_block () const
	{
		nano::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		auto error (false);
		auto result (std::make_shared<Block> (error, stream));
		assert (!error);
		return result;
	}

	explicit operator std::shared_ptr<nano::send_block> () const
	{
		return convert_to_block<nano::send_block> ();
	}

	explicit operator std::shared_ptr<nano::receive_block> () const
	{
		return convert_to_block<nano::receive_block> ();
	}

	explicit operator std::shared_ptr<nano::open_block> () const
	{
		return convert_to_block<nano::open_block> ();
	}

	explicit operator std::shared_ptr<nano::change_block> () const
	{
		return convert_to_block<nano::change_block> ();
	}

	explicit operator std::shared_ptr<nano::state_block> () const
	{
		return convert_to_block<nano::state_block> ();
	}

	explicit operator std::shared_ptr<nano::vote> () const
	{
		nano::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		auto error (false);
		auto result (nano::make_shared<nano::vote> (error, stream));
		assert (!error);
		return result;
	}

	explicit operator uint64_t () const
	{
		uint64_t result;
		nano::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		auto error (nano::try_read (stream, result));
		assert (!error);
		boost::endian::big_to_native_inplace (result);
		return result;
	}

	operator Val * () const
	{
		// Allow passing a temporary to a non-c++ function which doesn't have constness
		return const_cast<Val *> (&value);
	}

	operator Val const & () const
	{
		return value;
	}

	/** Must be specialized in the sub-class */
	void * data () const;
	size_t size () const;

	Val value;
	std::shared_ptr<std::vector<uint8_t>> buffer;
	nano::epoch epoch{ nano::epoch::unspecified };
};

class block_sideband final
{
public:
	block_sideband () = default;
	block_sideband (nano::block_type, nano::account const &, nano::block_hash const &, nano::amount const &, uint64_t, uint64_t);
	void serialize (nano::stream &) const;
	bool deserialize (nano::stream &);
	static size_t size (nano::block_type);
	nano::block_type type{ nano::block_type::invalid };
	nano::block_hash successor{ 0 };
	nano::account account{ 0 };
	nano::amount balance{ 0 };
	uint64_t height{ 0 };
	uint64_t timestamp{ 0 };
};
class transaction;
class block_store;

/**
 * Summation visitor for blocks, supporting amount and balance computations. These
 * computations are mutually dependant. The natural solution is to use mutual recursion
 * between balance and amount visitors, but this leads to very deep stacks. Hence, the
 * summation visitor uses an iterative approach.
 */
class summation_visitor final : public nano::block_visitor
{
	enum summation_type
	{
		invalid = 0,
		balance = 1,
		amount = 2
	};

	/** Represents an invocation frame */
	class frame final
	{
	public:
		frame (summation_type type_a, nano::block_hash balance_hash_a, nano::block_hash amount_hash_a) :
		type (type_a), balance_hash (balance_hash_a), amount_hash (amount_hash_a)
		{
		}

		/** The summation type guides the block visitor handlers */
		summation_type type{ invalid };
		/** Accumulated balance or amount */
		nano::uint128_t sum{ 0 };
		/** The current balance hash */
		nano::block_hash balance_hash{ 0 };
		/** The current amount hash */
		nano::block_hash amount_hash{ 0 };
		/** If true, this frame is awaiting an invocation result */
		bool awaiting_result{ false };
		/** Set by the invoked frame, representing the return value */
		nano::uint128_t incoming_result{ 0 };
	};

public:
	summation_visitor (nano::transaction const &, nano::block_store const &);
	virtual ~summation_visitor () = default;
	/** Computes the balance as of \p block_hash */
	nano::uint128_t compute_balance (nano::block_hash const & block_hash);
	/** Computes the amount delta between \p block_hash and its predecessor */
	nano::uint128_t compute_amount (nano::block_hash const & block_hash);

protected:
	nano::transaction const & transaction;
	nano::block_store const & store;
	nano::network_params network_params;

	/** The final result */
	nano::uint128_t result{ 0 };
	/** The current invocation frame */
	frame * current{ nullptr };
	/** Invocation frames */
	std::stack<frame> frames;
	/** Push a copy of \p hash of the given summation \p type */
	nano::summation_visitor::frame push (nano::summation_visitor::summation_type type, nano::block_hash const & hash);
	void sum_add (nano::uint128_t addend_a);
	void sum_set (nano::uint128_t value_a);
	/** The epilogue yields the result to previous frame, if any */
	void epilogue ();

	nano::uint128_t compute_internal (nano::summation_visitor::summation_type type, nano::block_hash const &);
	void send_block (nano::send_block const &) override;
	void receive_block (nano::receive_block const &) override;
	void open_block (nano::open_block const &) override;
	void change_block (nano::change_block const &) override;
	void state_block (nano::state_block const &) override;
};

/**
 * Determine the representative for this block
 */
class representative_visitor final : public nano::block_visitor
{
public:
	representative_visitor (nano::transaction const & transaction_a, nano::block_store & store_a);
	~representative_visitor () = default;
	void compute (nano::block_hash const & hash_a);
	void send_block (nano::send_block const & block_a) override;
	void receive_block (nano::receive_block const & block_a) override;
	void open_block (nano::open_block const & block_a) override;
	void change_block (nano::change_block const & block_a) override;
	void state_block (nano::state_block const & block_a) override;
	nano::transaction const & transaction;
	nano::block_store & store;
	nano::block_hash current;
	nano::block_hash result;
};
template <typename T, typename U>
class store_iterator_impl
{
public:
	virtual ~store_iterator_impl () = default;
	virtual nano::store_iterator_impl<T, U> & operator++ () = 0;
	virtual bool operator== (nano::store_iterator_impl<T, U> const & other_a) const = 0;
	virtual bool is_end_sentinal () const = 0;
	virtual void fill (std::pair<T, U> &) const = 0;
	nano::store_iterator_impl<T, U> & operator= (nano::store_iterator_impl<T, U> const &) = delete;
	bool operator== (nano::store_iterator_impl<T, U> const * other_a) const
	{
		return (other_a != nullptr && *this == *other_a) || (other_a == nullptr && is_end_sentinal ());
	}
	bool operator!= (nano::store_iterator_impl<T, U> const & other_a) const
	{
		return !(*this == other_a);
	}
};
/**
 * Iterates the key/value pairs of a transaction
 */
template <typename T, typename U>
class store_iterator final
{
public:
	store_iterator (std::nullptr_t)
	{
	}
	store_iterator (std::unique_ptr<nano::store_iterator_impl<T, U>> impl_a) :
	impl (std::move (impl_a))
	{
		impl->fill (current);
	}
	store_iterator (nano::store_iterator<T, U> && other_a) :
	current (std::move (other_a.current)),
	impl (std::move (other_a.impl))
	{
	}
	nano::store_iterator<T, U> & operator++ ()
	{
		++*impl;
		impl->fill (current);
		return *this;
	}
	nano::store_iterator<T, U> & operator= (nano::store_iterator<T, U> && other_a)
	{
		impl = std::move (other_a.impl);
		current = std::move (other_a.current);
		return *this;
	}
	nano::store_iterator<T, U> & operator= (nano::store_iterator<T, U> const &) = delete;
	std::pair<T, U> * operator-> ()
	{
		return &current;
	}
	bool operator== (nano::store_iterator<T, U> const & other_a) const
	{
		return (impl == nullptr && other_a.impl == nullptr) || (impl != nullptr && *impl == other_a.impl.get ()) || (other_a.impl != nullptr && *other_a.impl == impl.get ());
	}
	bool operator!= (nano::store_iterator<T, U> const & other_a) const
	{
		return !(*this == other_a);
	}

private:
	std::pair<T, U> current;
	std::unique_ptr<nano::store_iterator_impl<T, U>> impl;
};

class transaction_impl
{
public:
	virtual ~transaction_impl () = default;
	virtual void * get_handle () const = 0;
};

class read_transaction_impl : public transaction_impl
{
public:
	virtual void reset () const = 0;
	virtual void renew () const = 0;
};

class write_transaction_impl : public transaction_impl
{
public:
	virtual void commit () const = 0;
	virtual void renew () = 0;
};

class transaction
{
public:
	virtual ~transaction () = default;
	virtual void * get_handle () const = 0;
};

/**
 * RAII wrapper of a read MDB_txn where the constructor starts the transaction
 * and the destructor aborts it.
 */
class read_transaction final : public transaction
{
public:
	explicit read_transaction (std::unique_ptr<nano::read_transaction_impl> read_transaction_impl);
	void * get_handle () const override;
	void reset () const;
	void renew () const;
	void refresh () const;

private:
	std::unique_ptr<nano::read_transaction_impl> impl;
};

/**
 * RAII wrapper of a read-write MDB_txn where the constructor starts the transaction
 * and the destructor commits it.
 */
class write_transaction final : public transaction
{
public:
	explicit write_transaction (std::unique_ptr<nano::write_transaction_impl> write_transaction_impl);
	void * get_handle () const override;
	void commit () const;
	void renew ();

private:
	std::unique_ptr<nano::write_transaction_impl> impl;
};

/**
 * Manages block storage and iteration
 */
class block_store
{
public:
	virtual ~block_store () = default;
	virtual void initialize (nano::transaction const &, nano::genesis const &) = 0;
	virtual void block_put (nano::transaction const &, nano::block_hash const &, nano::block const &, nano::block_sideband const &, nano::epoch version = nano::epoch::epoch_0) = 0;
	virtual nano::block_hash block_successor (nano::transaction const &, nano::block_hash const &) const = 0;
	virtual void block_successor_clear (nano::transaction const &, nano::block_hash const &) = 0;
	virtual std::shared_ptr<nano::block> block_get (nano::transaction const &, nano::block_hash const &, nano::block_sideband * = nullptr) const = 0;
	virtual std::shared_ptr<nano::block> block_random (nano::transaction const &) = 0;
	virtual void block_del (nano::transaction const &, nano::block_hash const &) = 0;
	virtual bool block_exists (nano::transaction const &, nano::block_hash const &) = 0;
	virtual bool block_exists (nano::transaction const &, nano::block_type, nano::block_hash const &) = 0;
	virtual nano::block_counts block_count (nano::transaction const &) = 0;
	virtual bool root_exists (nano::transaction const &, nano::uint256_union const &) = 0;
	virtual bool source_exists (nano::transaction const &, nano::block_hash const &) = 0;
	virtual nano::account block_account (nano::transaction const &, nano::block_hash const &) const = 0;

	virtual void frontier_put (nano::transaction const &, nano::block_hash const &, nano::account const &) = 0;
	virtual nano::account frontier_get (nano::transaction const &, nano::block_hash const &) const = 0;
	virtual void frontier_del (nano::transaction const &, nano::block_hash const &) = 0;

	virtual void account_put (nano::transaction const &, nano::account const &, nano::account_info const &) = 0;
	virtual bool account_get (nano::transaction const &, nano::account const &, nano::account_info &) = 0;
	virtual void account_del (nano::transaction const &, nano::account const &) = 0;
	virtual bool account_exists (nano::transaction const &, nano::account const &) = 0;
	virtual size_t account_count (nano::transaction const &) = 0;
	virtual void confirmation_height_clear (nano::transaction const &, nano::account const & account, nano::account_info const & account_info) = 0;
	virtual void confirmation_height_clear (nano::transaction const &) = 0;
	virtual uint64_t cemented_count (nano::transaction const &) = 0;
	virtual nano::store_iterator<nano::account, nano::account_info> latest_v0_begin (nano::transaction const &, nano::account const &) = 0;
	virtual nano::store_iterator<nano::account, nano::account_info> latest_v0_begin (nano::transaction const &) = 0;
	virtual nano::store_iterator<nano::account, nano::account_info> latest_v0_end () = 0;
	virtual nano::store_iterator<nano::account, nano::account_info> latest_v1_begin (nano::transaction const &, nano::account const &) = 0;
	virtual nano::store_iterator<nano::account, nano::account_info> latest_v1_begin (nano::transaction const &) = 0;
	virtual nano::store_iterator<nano::account, nano::account_info> latest_v1_end () = 0;
	virtual nano::store_iterator<nano::account, nano::account_info> latest_begin (nano::transaction const &, nano::account const &) = 0;
	virtual nano::store_iterator<nano::account, nano::account_info> latest_begin (nano::transaction const &) = 0;
	virtual nano::store_iterator<nano::account, nano::account_info> latest_end () = 0;

	virtual void pending_put (nano::transaction const &, nano::pending_key const &, nano::pending_info const &) = 0;
	virtual void pending_del (nano::transaction const &, nano::pending_key const &) = 0;
	virtual bool pending_get (nano::transaction const &, nano::pending_key const &, nano::pending_info &) = 0;
	virtual bool pending_exists (nano::transaction const &, nano::pending_key const &) = 0;
	virtual nano::store_iterator<nano::pending_key, nano::pending_info> pending_v0_begin (nano::transaction const &, nano::pending_key const &) = 0;
	virtual nano::store_iterator<nano::pending_key, nano::pending_info> pending_v0_begin (nano::transaction const &) = 0;
	virtual nano::store_iterator<nano::pending_key, nano::pending_info> pending_v0_end () = 0;
	virtual nano::store_iterator<nano::pending_key, nano::pending_info> pending_v1_begin (nano::transaction const &, nano::pending_key const &) = 0;
	virtual nano::store_iterator<nano::pending_key, nano::pending_info> pending_v1_begin (nano::transaction const &) = 0;
	virtual nano::store_iterator<nano::pending_key, nano::pending_info> pending_v1_end () = 0;
	virtual nano::store_iterator<nano::pending_key, nano::pending_info> pending_begin (nano::transaction const &, nano::pending_key const &) = 0;
	virtual nano::store_iterator<nano::pending_key, nano::pending_info> pending_begin (nano::transaction const &) = 0;
	virtual nano::store_iterator<nano::pending_key, nano::pending_info> pending_end () = 0;

	virtual bool block_info_get (nano::transaction const &, nano::block_hash const &, nano::block_info &) const = 0;
	virtual nano::uint128_t block_balance (nano::transaction const &, nano::block_hash const &) = 0;
	virtual nano::uint128_t block_balance_calculated (std::shared_ptr<nano::block>, nano::block_sideband const &) const = 0;
	virtual nano::epoch block_version (nano::transaction const &, nano::block_hash const &) = 0;

	virtual nano::uint128_t representation_get (nano::transaction const &, nano::account const &) = 0;
	virtual void representation_put (nano::transaction const &, nano::account const &, nano::uint128_t const &) = 0;
	virtual void representation_add (nano::transaction const &, nano::account const &, nano::uint128_t const &) = 0;
	virtual nano::store_iterator<nano::account, nano::uint128_union> representation_begin (nano::transaction const &) = 0;
	virtual nano::store_iterator<nano::account, nano::uint128_union> representation_end () = 0;

	virtual void unchecked_clear (nano::transaction const &) = 0;
	virtual void unchecked_put (nano::transaction const &, nano::unchecked_key const &, nano::unchecked_info const &) = 0;
	virtual void unchecked_put (nano::transaction const &, nano::block_hash const &, std::shared_ptr<nano::block> const &) = 0;
	virtual std::vector<nano::unchecked_info> unchecked_get (nano::transaction const &, nano::block_hash const &) = 0;
	virtual void unchecked_del (nano::transaction const &, nano::unchecked_key const &) = 0;
	virtual nano::store_iterator<nano::unchecked_key, nano::unchecked_info> unchecked_begin (nano::transaction const &) = 0;
	virtual nano::store_iterator<nano::unchecked_key, nano::unchecked_info> unchecked_begin (nano::transaction const &, nano::unchecked_key const &) = 0;
	virtual nano::store_iterator<nano::unchecked_key, nano::unchecked_info> unchecked_end () = 0;
	virtual size_t unchecked_count (nano::transaction const &) = 0;

	// Return latest vote for an account from store
	virtual std::shared_ptr<nano::vote> vote_get (nano::transaction const &, nano::account const &) = 0;
	// Populate vote with the next sequence number
	virtual std::shared_ptr<nano::vote> vote_generate (nano::transaction const &, nano::account const &, nano::raw_key const &, std::shared_ptr<nano::block>) = 0;
	virtual std::shared_ptr<nano::vote> vote_generate (nano::transaction const &, nano::account const &, nano::raw_key const &, std::vector<nano::block_hash>) = 0;
	// Return either vote or the stored vote with a higher sequence number
	virtual std::shared_ptr<nano::vote> vote_max (nano::transaction const &, std::shared_ptr<nano::vote>) = 0;
	// Return latest vote for an account considering the vote cache
	virtual std::shared_ptr<nano::vote> vote_current (nano::transaction const &, nano::account const &) = 0;
	virtual void flush (nano::transaction const &) = 0;
	virtual nano::store_iterator<nano::account, std::shared_ptr<nano::vote>> vote_begin (nano::transaction const &) = 0;
	virtual nano::store_iterator<nano::account, std::shared_ptr<nano::vote>> vote_end () = 0;

	virtual void online_weight_put (nano::transaction const &, uint64_t, nano::amount const &) = 0;
	virtual void online_weight_del (nano::transaction const &, uint64_t) = 0;
	virtual nano::store_iterator<uint64_t, nano::amount> online_weight_begin (nano::transaction const &) = 0;
	virtual nano::store_iterator<uint64_t, nano::amount> online_weight_end () = 0;
	virtual size_t online_weight_count (nano::transaction const &) const = 0;
	virtual void online_weight_clear (nano::transaction const &) = 0;

	virtual void version_put (nano::transaction const &, int) = 0;
	virtual int version_get (nano::transaction const &) const = 0;

	virtual void peer_put (nano::transaction const & transaction_a, nano::endpoint_key const & endpoint_a) = 0;
	virtual void peer_del (nano::transaction const & transaction_a, nano::endpoint_key const & endpoint_a) = 0;
	virtual bool peer_exists (nano::transaction const & transaction_a, nano::endpoint_key const & endpoint_a) const = 0;
	virtual size_t peer_count (nano::transaction const & transaction_a) const = 0;
	virtual void peer_clear (nano::transaction const & transaction_a) = 0;
	virtual nano::store_iterator<nano::endpoint_key, nano::no_value> peers_begin (nano::transaction const & transaction_a) = 0;
	virtual nano::store_iterator<nano::endpoint_key, nano::no_value> peers_end () = 0;

	virtual uint64_t block_account_height (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const = 0;
	virtual void serialize_mdb_tracker (boost::property_tree::ptree &, std::chrono::milliseconds, std::chrono::milliseconds) = 0;

	/** Start read-write transaction */
	virtual nano::write_transaction tx_begin_write () = 0;

	/** Start read-only transaction */
	virtual nano::read_transaction tx_begin_read () = 0;
};

template <typename Val>
class block_predecessor_set;

/** This base class implements the block_store interface functions which have DB agnostic functionality */
template <class Val>
class block_store_partial : public block_store
{
public:
	using block_store::block_exists;
	using block_store::unchecked_put;

	friend class nano::block_predecessor_set<Val>;

	std::mutex cache_mutex;

	/**
	 * If using a different store version than the latest then you may need
	 * to modify some of the objects in the store to be appropriate for the version before an upgrade.
	 */
	void initialize (nano::transaction const & transaction_a, nano::genesis const & genesis_a) override
	{
		auto hash_l (genesis_a.hash ());
		assert (latest_v0_begin (transaction_a) == latest_v0_end ());
		assert (latest_v1_begin (transaction_a) == latest_v1_end ());
		nano::block_sideband sideband (nano::block_type::open, network_params.ledger.genesis_account, 0, network_params.ledger.genesis_amount, 1, nano::seconds_since_epoch ());
		block_put (transaction_a, hash_l, *genesis_a.open, sideband);
		account_put (transaction_a, network_params.ledger.genesis_account, { hash_l, genesis_a.open->hash (), genesis_a.open->hash (), std::numeric_limits<nano::uint128_t>::max (), nano::seconds_since_epoch (), 1, 1, nano::epoch::epoch_0 });
		representation_put (transaction_a, network_params.ledger.genesis_account, std::numeric_limits<nano::uint128_t>::max ());
		frontier_put (transaction_a, hash_l, network_params.ledger.genesis_account);
	}

	nano::uint128_t block_balance (nano::transaction const & transaction_a, nano::block_hash const & hash_a) override
	{
		nano::block_sideband sideband;
		auto block (block_get (transaction_a, hash_a, &sideband));
		nano::uint128_t result (block_balance_calculated (block, sideband));
		return result;
	}

	void representation_add (nano::transaction const & transaction_a, nano::block_hash const & source_a, nano::uint128_t const & amount_a) override
	{
		auto source_block (block_get (transaction_a, source_a));
		assert (source_block != nullptr);
		auto source_rep (source_block->representative ());
		auto source_previous (representation_get (transaction_a, source_rep));
		representation_put (transaction_a, source_rep, source_previous + amount_a);
	}

	bool account_exists (nano::transaction const & transaction_a, nano::account const & account_a) override
	{
		auto iterator (latest_begin (transaction_a, account_a));
		return iterator != latest_end () && nano::account (iterator->first) == account_a;
	}

	void confirmation_height_clear (nano::transaction const & transaction_a, nano::account const & account, nano::account_info const & account_info) override
	{
		nano::account_info info_copy (account_info);
		if (info_copy.confirmation_height > 0)
		{
			info_copy.confirmation_height = 0;
			account_put (transaction_a, account, info_copy);
		}
	}

	void confirmation_height_clear (nano::transaction const & transaction_a) override
	{
		for (auto i (latest_begin (transaction_a)), n (latest_end ()); i != n; ++i)
		{
			confirmation_height_clear (transaction_a, i->first, i->second);
		}
	}

	bool pending_exists (nano::transaction const & transaction_a, nano::pending_key const & key_a) override
	{
		auto iterator (pending_begin (transaction_a, key_a));
		return iterator != pending_end () && nano::pending_key (iterator->first) == key_a;
	}

	std::vector<nano::unchecked_info> unchecked_get (nano::transaction const & transaction_a, nano::block_hash const & hash_a) override
	{
		std::vector<nano::unchecked_info> result;
		for (auto i (unchecked_begin (transaction_a, nano::unchecked_key (hash_a, 0))), n (unchecked_end ()); i != n && nano::block_hash (i->first.key ()) == hash_a; ++i)
		{
			nano::unchecked_info const & unchecked_info (i->second);
			result.push_back (unchecked_info);
		}
		return result;
	}

	void block_put (nano::transaction const & transaction_a, nano::block_hash const & hash_a, nano::block const & block_a, nano::block_sideband const & sideband_a, nano::epoch epoch_a = nano::epoch::epoch_0) override
	{
		assert (block_a.type () == sideband_a.type);
		assert (sideband_a.successor.is_zero () || block_exists (transaction_a, sideband_a.successor));
		std::vector<uint8_t> vector;
		{
			nano::vectorstream stream (vector);
			block_a.serialize (stream);
			sideband_a.serialize (stream);
		}
		block_raw_put (transaction_a, vector, block_a.type (), epoch_a, hash_a);
		nano::block_predecessor_set<Val> predecessor (transaction_a, *this);
		block_a.visit (predecessor);
		assert (block_a.previous ().is_zero () || block_successor (transaction_a, block_a.previous ()) == hash_a);
	}

	// Converts a block hash to a block height
	uint64_t block_account_height (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const override
	{
		nano::block_sideband sideband;
		auto block = block_get (transaction_a, hash_a, &sideband);
		assert (block != nullptr);
		return sideband.height;
	}

	std::shared_ptr<nano::block> block_get (nano::transaction const & transaction_a, nano::block_hash const & hash_a, nano::block_sideband * sideband_a = nullptr) const override
	{
		nano::block_type type;
		auto value (block_raw_get (transaction_a, hash_a, type));
		std::shared_ptr<nano::block> result;
		if (value.size () != 0)
		{
			nano::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
			result = nano::deserialize_block (stream, type);
			assert (result != nullptr);
			if (sideband_a)
			{
				sideband_a->type = type;
				if (full_sideband (transaction_a) || entry_has_sideband (value.size (), type))
				{
					auto error (sideband_a->deserialize (stream));
					assert (!error);
				}
				else
				{
					// Reconstruct sideband data for block.
					sideband_a->account = block_account_computed (transaction_a, hash_a);
					sideband_a->balance = block_balance_computed (transaction_a, hash_a);
					sideband_a->successor = block_successor (transaction_a, hash_a);
					sideband_a->height = 0;
					sideband_a->timestamp = 0;
				}
			}
		}
		return result;
	}

	bool block_exists (nano::transaction const & tx_a, nano::block_hash const & hash_a) override
	{
		// Table lookups are ordered by match probability
		// clang-format off
		return
			block_exists (tx_a, nano::block_type::state, hash_a) ||
			block_exists (tx_a, nano::block_type::send, hash_a) ||
			block_exists (tx_a, nano::block_type::receive, hash_a) ||
			block_exists (tx_a, nano::block_type::open, hash_a) ||
			block_exists (tx_a, nano::block_type::change, hash_a);
		// clang-format on
	}

	bool root_exists (nano::transaction const & transaction_a, nano::uint256_union const & root_a) override
	{
		return block_exists (transaction_a, root_a) || account_exists (transaction_a, root_a);
	}

	bool source_exists (nano::transaction const & transaction_a, nano::block_hash const & source_a) override
	{
		return block_exists (transaction_a, nano::block_type::state, source_a) || block_exists (transaction_a, nano::block_type::send, source_a);
	}

	nano::account block_account (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const override
	{
		nano::block_sideband sideband;
		auto block (block_get (transaction_a, hash_a, &sideband));
		nano::account result (block->account ());
		if (result.is_zero ())
		{
			result = sideband.account;
		}
		assert (!result.is_zero ());
		return result;
	}

	nano::uint128_t block_balance_calculated (std::shared_ptr<nano::block> block_a, nano::block_sideband const & sideband_a) const override
	{
		nano::uint128_t result;
		switch (block_a->type ())
		{
			case nano::block_type::open:
			case nano::block_type::receive:
			case nano::block_type::change:
				result = sideband_a.balance.number ();
				break;
			case nano::block_type::send:
				result = boost::polymorphic_downcast<nano::send_block *> (block_a.get ())->hashables.balance.number ();
				break;
			case nano::block_type::state:
				result = boost::polymorphic_downcast<nano::state_block *> (block_a.get ())->hashables.balance.number ();
				break;
			case nano::block_type::invalid:
			case nano::block_type::not_a_block:
				release_assert (false);
				break;
		}
		return result;
	}

	nano::block_hash block_successor (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const override
	{
		nano::block_type type;
		auto value (block_raw_get (transaction_a, hash_a, type));
		nano::block_hash result;
		if (value.size () != 0)
		{
			assert (value.size () >= result.bytes.size ());
			nano::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()) + block_successor_offset (transaction_a, value.size (), type), result.bytes.size ());
			auto error (nano::try_read (stream, result.bytes));
			assert (!error);
		}
		else
		{
			result.clear ();
		}
		return result;
	}

	bool full_sideband (nano::transaction const & transaction_a) const
	{
		return version_get (transaction_a) > 12;
	}

	void block_successor_clear (nano::transaction const & transaction_a, nano::block_hash const & hash_a) override
	{
		nano::block_type type;
		auto value (block_raw_get (transaction_a, hash_a, type));
		auto version (block_version (transaction_a, hash_a));
		assert (value.size () != 0);
		std::vector<uint8_t> data (static_cast<uint8_t *> (value.data ()), static_cast<uint8_t *> (value.data ()) + value.size ());
		std::fill_n (data.begin () + block_successor_offset (transaction_a, value.size (), type), sizeof (nano::uint256_union), 0);
		block_raw_put (transaction_a, data, type, version, hash_a);
	}

	uint64_t cemented_count (nano::transaction const & transaction_a) override
	{
		uint64_t sum = 0;
		for (auto i (latest_begin (transaction_a)), n (latest_end ()); i != n; ++i)
		{
			nano::account_info const & info (i->second);
			sum += info.confirmation_height;
		}
		return sum;
	}

	void unchecked_put (nano::transaction const & transaction_a, nano::block_hash const & hash_a, std::shared_ptr<nano::block> const & block_a) override
	{
		nano::unchecked_key key (hash_a, block_a->hash ());
		nano::unchecked_info info (block_a, block_a->account (), nano::seconds_since_epoch (), nano::signature_verification::unknown);
		unchecked_put (transaction_a, key, info);
	}

	std::shared_ptr<nano::vote> vote_current (nano::transaction const & transaction_a, nano::account const & account_a) override
	{
		assert (!cache_mutex.try_lock ());
		std::shared_ptr<nano::vote> result;
		auto existing (vote_cache_l1.find (account_a));
		auto have_existing (true);
		if (existing == vote_cache_l1.end ())
		{
			existing = vote_cache_l2.find (account_a);
			if (existing == vote_cache_l2.end ())
			{
				have_existing = false;
			}
		}
		if (have_existing)
		{
			result = existing->second;
		}
		else
		{
			result = vote_get (transaction_a, account_a);
		}
		return result;
	}

	std::shared_ptr<nano::vote> vote_generate (nano::transaction const & transaction_a, nano::account const & account_a, nano::raw_key const & key_a, std::shared_ptr<nano::block> block_a) override
	{
		std::lock_guard<std::mutex> lock (cache_mutex);
		auto result (vote_current (transaction_a, account_a));
		uint64_t sequence ((result ? result->sequence : 0) + 1);
		result = std::make_shared<nano::vote> (account_a, key_a, sequence, block_a);
		vote_cache_l1[account_a] = result;
		return result;
	}

	std::shared_ptr<nano::vote> vote_generate (nano::transaction const & transaction_a, nano::account const & account_a, nano::raw_key const & key_a, std::vector<nano::block_hash> blocks_a) override
	{
		std::lock_guard<std::mutex> lock (cache_mutex);
		auto result (vote_current (transaction_a, account_a));
		uint64_t sequence ((result ? result->sequence : 0) + 1);
		result = std::make_shared<nano::vote> (account_a, key_a, sequence, blocks_a);
		vote_cache_l1[account_a] = result;
		return result;
	}

	std::shared_ptr<nano::vote> vote_max (nano::transaction const & transaction_a, std::shared_ptr<nano::vote> vote_a) override
	{
		std::lock_guard<std::mutex> lock (cache_mutex);
		auto current (vote_current (transaction_a, vote_a->account));
		auto result (vote_a);
		if (current != nullptr && current->sequence > result->sequence)
		{
			result = current;
		}
		vote_cache_l1[vote_a->account] = result;
		return result;
	}

	virtual void block_raw_put (nano::transaction const & transaction_a, std::vector<uint8_t> const & data, nano::block_type block_type_a, nano::epoch epoch_a, nano::block_hash const & hash_a) = 0;

protected:
	nano::network_params network_params;
	std::unordered_map<nano::account, std::shared_ptr<nano::vote>> vote_cache_l1;
	std::unordered_map<nano::account, std::shared_ptr<nano::vote>> vote_cache_l2;

	bool entry_has_sideband (size_t entry_size_a, nano::block_type type_a) const
	{
		return entry_size_a == nano::block::size (type_a) + nano::block_sideband::size (type_a);
	}

	nano::db_val<Val> block_raw_get (nano::transaction const & transaction_a, nano::block_hash const & hash_a, nano::block_type & type_a) const
	{
		nano::db_val<Val> result;
		// Table lookups are ordered by match probability
		nano::block_type block_types[]{ nano::block_type::state, nano::block_type::send, nano::block_type::receive, nano::block_type::open, nano::block_type::change };
		for (auto current_type : block_types)
		{
			auto db_val (block_raw_get_by_type (transaction_a, hash_a, current_type));
			if (db_val.is_initialized ())
			{
				type_a = current_type;
				result = db_val.get ();
				break;
			}
		}

		return result;
	}

	// Return account containing hash
	nano::account block_account_computed (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const
	{
		assert (!full_sideband (transaction_a));
		nano::account result (0);
		auto hash (hash_a);
		while (result.is_zero ())
		{
			auto block (block_get (transaction_a, hash));
			assert (block);
			result = block->account ();
			if (result.is_zero ())
			{
				auto type (nano::block_type::invalid);
				auto value (block_raw_get (transaction_a, block->previous (), type));
				if (entry_has_sideband (value.size (), type))
				{
					result = block_account (transaction_a, block->previous ());
				}
				else
				{
					nano::block_info block_info;
					if (!block_info_get (transaction_a, hash, block_info))
					{
						result = block_info.account;
					}
					else
					{
						result = frontier_get (transaction_a, hash);
						if (result.is_zero ())
						{
							auto successor (block_successor (transaction_a, hash));
							assert (!successor.is_zero ());
							hash = successor;
						}
					}
				}
			}
		}
		assert (!result.is_zero ());
		return result;
	}

	nano::uint128_t block_balance_computed (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const
	{
		assert (!full_sideband (transaction_a));
		summation_visitor visitor (transaction_a, *this);
		return visitor.compute_balance (hash_a);
	}

	size_t block_successor_offset (nano::transaction const & transaction_a, size_t entry_size_a, nano::block_type type_a) const
	{
		size_t result;
		if (full_sideband (transaction_a) || entry_has_sideband (entry_size_a, type_a))
		{
			result = entry_size_a - nano::block_sideband::size (type_a);
		}
		else
		{
			// Read old successor-only sideband
			assert (entry_size_a == nano::block::size (type_a) + sizeof (nano::uint256_union));
			result = entry_size_a - sizeof (nano::uint256_union);
		}
		return result;
	}

	virtual boost::optional<DB_val> block_raw_get_by_type (nano::transaction const &, nano::block_hash const &, nano::block_type &) const = 0;
};

/**
 * Fill in our predecessors
 */
template <class Val>
class block_predecessor_set : public nano::block_visitor
{
public:
	block_predecessor_set (nano::transaction const & transaction_a, nano::block_store_partial<Val> & store_a) :
	transaction (transaction_a),
	store (store_a)
	{
	}
	virtual ~block_predecessor_set () = default;
	void fill_value (nano::block const & block_a)
	{
		auto hash (block_a.hash ());
		nano::block_type type;
		auto value (store.block_raw_get (transaction, block_a.previous (), type));
		auto version (store.block_version (transaction, block_a.previous ()));
		assert (value.size () != 0);
		std::vector<uint8_t> data (static_cast<uint8_t *> (value.data ()), static_cast<uint8_t *> (value.data ()) + value.size ());
		std::copy (hash.bytes.begin (), hash.bytes.end (), data.begin () + store.block_successor_offset (transaction, value.size (), type));
		store.block_raw_put (transaction, data, type, version, block_a.previous ());
	}
	void send_block (nano::send_block const & block_a) override
	{
		fill_value (block_a);
	}
	void receive_block (nano::receive_block const & block_a) override
	{
		fill_value (block_a);
	}
	void open_block (nano::open_block const & block_a) override
	{
		// Open blocks don't have a predecessor
	}
	void change_block (nano::change_block const & block_a) override
	{
		fill_value (block_a);
	}
	void state_block (nano::state_block const & block_a) override
	{
		if (!block_a.previous ().is_zero ())
		{
			fill_value (block_a);
		}
	}
	nano::transaction const & transaction;
	nano::block_store_partial<Val> & store;
};
}
