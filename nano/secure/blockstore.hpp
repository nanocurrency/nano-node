#pragma once

#include <nano/secure/common.hpp>
#include <stack>

namespace nano
{
class block_sideband
{
public:
	block_sideband () = default;
	block_sideband (nano::block_type, nano::account const &, nano::block_hash const &, nano::amount const &, uint64_t, uint64_t);
	void serialize (nano::stream &) const;
	bool deserialize (nano::stream &);
	static size_t size (nano::block_type);
	nano::block_type type;
	nano::block_hash successor;
	nano::account account;
	nano::amount balance;
	uint64_t height;
	uint64_t timestamp;
};
class transaction;
class block_store;

/**
 * Summation visitor for blocks, supporting amount and balance computations. These
 * computations are mutually dependant. The natural solution is to use mutual recursion
 * between balance and amount visitors, but this leads to very deep stacks. Hence, the
 * summation visitor uses an iterative approach.
 */
class summation_visitor : public nano::block_visitor
{
	enum summation_type
	{
		invalid = 0,
		balance = 1,
		amount = 2
	};

	/** Represents an invocation frame */
	class frame
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
	summation_visitor (nano::transaction const &, nano::block_store &);
	virtual ~summation_visitor () = default;
	/** Computes the balance as of \p block_hash */
	nano::uint128_t compute_balance (nano::block_hash const & block_hash);
	/** Computes the amount delta between \p block_hash and its predecessor */
	nano::uint128_t compute_amount (nano::block_hash const & block_hash);

protected:
	nano::transaction const & transaction;
	nano::block_store & store;

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
class representative_visitor : public nano::block_visitor
{
public:
	representative_visitor (nano::transaction const & transaction_a, nano::block_store & store_a);
	virtual ~representative_visitor () = default;
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
class store_iterator
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

class block_predecessor_set;

class transaction_impl
{
public:
	virtual ~transaction_impl () = default;
};
/**
 * RAII wrapper of MDB_txn where the constructor starts the transaction
 * and the destructor commits it.
 */
class transaction
{
public:
	std::unique_ptr<nano::transaction_impl> impl;
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
	virtual nano::block_hash block_successor (nano::transaction const &, nano::block_hash const &) = 0;
	virtual void block_successor_clear (nano::transaction const &, nano::block_hash const &) = 0;
	virtual std::shared_ptr<nano::block> block_get (nano::transaction const &, nano::block_hash const &, nano::block_sideband * = nullptr) = 0;
	virtual std::shared_ptr<nano::block> block_random (nano::transaction const &) = 0;
	virtual void block_del (nano::transaction const &, nano::block_hash const &) = 0;
	virtual bool block_exists (nano::transaction const &, nano::block_hash const &) = 0;
	virtual bool block_exists (nano::transaction const &, nano::block_type, nano::block_hash const &) = 0;
	virtual nano::block_counts block_count (nano::transaction const &) = 0;
	virtual bool root_exists (nano::transaction const &, nano::uint256_union const &) = 0;
	virtual bool source_exists (nano::transaction const &, nano::block_hash const &) = 0;
	virtual nano::account block_account (nano::transaction const &, nano::block_hash const &) = 0;

	virtual void frontier_put (nano::transaction const &, nano::block_hash const &, nano::account const &) = 0;
	virtual nano::account frontier_get (nano::transaction const &, nano::block_hash const &) = 0;
	virtual void frontier_del (nano::transaction const &, nano::block_hash const &) = 0;

	virtual void account_put (nano::transaction const &, nano::account const &, nano::account_info const &) = 0;
	virtual bool account_get (nano::transaction const &, nano::account const &, nano::account_info &) = 0;
	virtual void account_del (nano::transaction const &, nano::account const &) = 0;
	virtual bool account_exists (nano::transaction const &, nano::account const &) = 0;
	virtual size_t account_count (nano::transaction const &) = 0;
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

	virtual bool block_info_get (nano::transaction const &, nano::block_hash const &, nano::block_info &) = 0;
	virtual nano::uint128_t block_balance (nano::transaction const &, nano::block_hash const &) = 0;
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
	virtual bool unchecked_exists (nano::transaction const &, nano::unchecked_key const &) = 0;
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
	virtual int version_get (nano::transaction const &) = 0;

	virtual void peer_put (nano::transaction const & transaction_a, nano::endpoint_key const & endpoint_a) = 0;
	virtual void peer_del (nano::transaction const & transaction_a, nano::endpoint_key const & endpoint_a) = 0;
	virtual bool peer_exists (nano::transaction const & transaction_a, nano::endpoint_key const & endpoint_a) const = 0;
	virtual size_t peer_count (nano::transaction const & transaction_a) const = 0;
	virtual void peer_clear (nano::transaction const & transaction_a) = 0;
	virtual nano::store_iterator<nano::endpoint_key, nano::no_value> peers_begin (nano::transaction const & transaction_a) = 0;
	virtual nano::store_iterator<nano::endpoint_key, nano::no_value> peers_end () = 0;

	// Requires a write transaction
	virtual nano::raw_key get_node_id (nano::transaction const &) = 0;

	/** Deletes the node ID from the store */
	virtual void delete_node_id (nano::transaction const &) = 0;

	/** Start read-write transaction */
	virtual nano::transaction tx_begin_write () = 0;

	/** Start read-only transaction */
	virtual nano::transaction tx_begin_read () = 0;

	/**
	 * Start a read-only or read-write transaction
	 * @param write If true, start a read-write transaction
	 */
	virtual nano::transaction tx_begin (bool write = false) = 0;
};
}
