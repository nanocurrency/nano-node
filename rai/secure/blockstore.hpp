#pragma once

#include <rai/secure/common.hpp>
#include <stack>

namespace rai
{
class transaction;
class block_store;

/**
 * Summation visitor for blocks, supporting amount and balance computations. These
 * computations are mutually dependant. The natural solution is to use mutual recursion
 * between balance and amount visitors, but this leads to very deep stacks. Hence, the
 * summation visitor uses an iterative approach.
 */
class summation_visitor : public rai::block_visitor
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
		frame (summation_type type_a, rai::block_hash balance_hash_a, rai::block_hash amount_hash_a) :
		type (type_a), balance_hash (balance_hash_a), amount_hash (amount_hash_a)
		{
		}

		/** The summation type guides the block visitor handlers */
		summation_type type{ invalid };
		/** Accumulated balance or amount */
		rai::uint128_t sum{ 0 };
		/** The current balance hash */
		rai::block_hash balance_hash{ 0 };
		/** The current amount hash */
		rai::block_hash amount_hash{ 0 };
		/** If true, this frame is awaiting an invocation result */
		bool awaiting_result{ false };
		/** Set by the invoked frame, representing the return value */
		rai::uint128_t incoming_result{ 0 };
	};

public:
	summation_visitor (rai::transaction const &, rai::block_store &);
	virtual ~summation_visitor () = default;
	/** Computes the balance as of \p block_hash */
	rai::uint128_t compute_balance (rai::block_hash const & block_hash);
	/** Computes the amount delta between \p block_hash and its predecessor */
	rai::uint128_t compute_amount (rai::block_hash const & block_hash);

protected:
	rai::transaction const & transaction;
	rai::block_store & store;

	/** The final result */
	rai::uint128_t result{ 0 };
	/** The current invocation frame */
	frame * current{ nullptr };
	/** Invocation frames */
	std::stack<frame> frames;
	/** Push a copy of \p hash of the given summation \p type */
	rai::summation_visitor::frame push (rai::summation_visitor::summation_type type, rai::block_hash const & hash);
	void sum_add (rai::uint128_t addend_a);
	void sum_set (rai::uint128_t value_a);
	/** The epilogue yields the result to previous frame, if any */
	void epilogue ();

	rai::uint128_t compute_internal (rai::summation_visitor::summation_type type, rai::block_hash const &);
	void send_block (rai::send_block const &) override;
	void receive_block (rai::receive_block const &) override;
	void open_block (rai::open_block const &) override;
	void change_block (rai::change_block const &) override;
	void state_block (rai::state_block const &) override;
};

/**
 * Determine the representative for this block
 */
class representative_visitor : public rai::block_visitor
{
public:
	representative_visitor (rai::transaction const & transaction_a, rai::block_store & store_a);
	virtual ~representative_visitor () = default;
	void compute (rai::block_hash const & hash_a);
	void send_block (rai::send_block const & block_a) override;
	void receive_block (rai::receive_block const & block_a) override;
	void open_block (rai::open_block const & block_a) override;
	void change_block (rai::change_block const & block_a) override;
	void state_block (rai::state_block const & block_a) override;
	rai::transaction const & transaction;
	rai::block_store & store;
	rai::block_hash current;
	rai::block_hash result;
};
template <typename T, typename U>
class store_iterator_impl
{
public:
	virtual ~store_iterator_impl () = default;
	virtual rai::store_iterator_impl<T, U> & operator++ () = 0;
	virtual bool operator== (rai::store_iterator_impl<T, U> const & other_a) const = 0;
	virtual bool is_end_sentinal () const = 0;
	virtual void fill (std::pair<T, U> &) const = 0;
	rai::store_iterator_impl<T, U> & operator= (rai::store_iterator_impl<T, U> const &) = delete;
	bool operator== (rai::store_iterator_impl<T, U> const * other_a) const
	{
		return (other_a != nullptr && *this == *other_a) || (other_a == nullptr && is_end_sentinal ());
	}
	bool operator!= (rai::store_iterator_impl<T, U> const & other_a) const
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
	store_iterator (std::unique_ptr<rai::store_iterator_impl<T, U>> impl_a) :
	impl (std::move (impl_a))
	{
		impl->fill (current);
	}
	store_iterator (rai::store_iterator<T, U> && other_a) :
	current (std::move (other_a.current)),
	impl (std::move (other_a.impl))
	{
	}
	rai::store_iterator<T, U> & operator++ ()
	{
		++*impl;
		impl->fill (current);
		return *this;
	}
	rai::store_iterator<T, U> & operator= (rai::store_iterator<T, U> && other_a)
	{
		impl = std::move (other_a.impl);
		current = std::move (other_a.current);
		return *this;
	}
	rai::store_iterator<T, U> & operator= (rai::store_iterator<T, U> const &) = delete;
	std::pair<T, U> * operator-> ()
	{
		return &current;
	}
	bool operator== (rai::store_iterator<T, U> const & other_a) const
	{
		return (impl == nullptr && other_a.impl == nullptr) || (impl != nullptr && *impl == other_a.impl.get ()) || (other_a.impl != nullptr && *other_a.impl == impl.get ());
	}
	bool operator!= (rai::store_iterator<T, U> const & other_a) const
	{
		return !(*this == other_a);
	}

private:
	std::pair<T, U> current;
	std::unique_ptr<rai::store_iterator_impl<T, U>> impl;
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
	std::unique_ptr<rai::transaction_impl> impl;
};

/**
 * Manages block storage and iteration
 */
class block_store
{
public:
	virtual ~block_store () = default;
	virtual void initialize (rai::transaction const &, rai::genesis const &) = 0;
	virtual void block_put (rai::transaction const &, rai::block_hash const &, rai::block const &, rai::block_hash const & = rai::block_hash (0), rai::epoch version = rai::epoch::epoch_0) = 0;
	virtual rai::block_hash block_successor (rai::transaction const &, rai::block_hash const &) = 0;
	virtual void block_successor_clear (rai::transaction const &, rai::block_hash const &) = 0;
	virtual std::shared_ptr<rai::block> block_get (rai::transaction const &, rai::block_hash const &) = 0;
	virtual std::shared_ptr<rai::block> block_random (rai::transaction const &) = 0;
	virtual void block_del (rai::transaction const &, rai::block_hash const &) = 0;
	virtual bool block_exists (rai::transaction const &, rai::block_hash const &) = 0;
	virtual bool block_exists (rai::transaction const &, rai::block_type, rai::block_hash const &) = 0;
	virtual rai::block_counts block_count (rai::transaction const &) = 0;
	virtual bool root_exists (rai::transaction const &, rai::uint256_union const &) = 0;

	virtual void frontier_put (rai::transaction const &, rai::block_hash const &, rai::account const &) = 0;
	virtual rai::account frontier_get (rai::transaction const &, rai::block_hash const &) = 0;
	virtual void frontier_del (rai::transaction const &, rai::block_hash const &) = 0;

	virtual void account_put (rai::transaction const &, rai::account const &, rai::account_info const &) = 0;
	virtual bool account_get (rai::transaction const &, rai::account const &, rai::account_info &) = 0;
	virtual void account_del (rai::transaction const &, rai::account const &) = 0;
	virtual bool account_exists (rai::transaction const &, rai::account const &) = 0;
	virtual size_t account_count (rai::transaction const &) = 0;
	virtual rai::store_iterator<rai::account, rai::account_info> latest_v0_begin (rai::transaction const &, rai::account const &) = 0;
	virtual rai::store_iterator<rai::account, rai::account_info> latest_v0_begin (rai::transaction const &) = 0;
	virtual rai::store_iterator<rai::account, rai::account_info> latest_v0_end () = 0;
	virtual rai::store_iterator<rai::account, rai::account_info> latest_v1_begin (rai::transaction const &, rai::account const &) = 0;
	virtual rai::store_iterator<rai::account, rai::account_info> latest_v1_begin (rai::transaction const &) = 0;
	virtual rai::store_iterator<rai::account, rai::account_info> latest_v1_end () = 0;
	virtual rai::store_iterator<rai::account, rai::account_info> latest_begin (rai::transaction const &, rai::account const &) = 0;
	virtual rai::store_iterator<rai::account, rai::account_info> latest_begin (rai::transaction const &) = 0;
	virtual rai::store_iterator<rai::account, rai::account_info> latest_end () = 0;

	virtual void pending_put (rai::transaction const &, rai::pending_key const &, rai::pending_info const &) = 0;
	virtual void pending_del (rai::transaction const &, rai::pending_key const &) = 0;
	virtual bool pending_get (rai::transaction const &, rai::pending_key const &, rai::pending_info &) = 0;
	virtual bool pending_exists (rai::transaction const &, rai::pending_key const &) = 0;
	virtual rai::store_iterator<rai::pending_key, rai::pending_info> pending_v0_begin (rai::transaction const &, rai::pending_key const &) = 0;
	virtual rai::store_iterator<rai::pending_key, rai::pending_info> pending_v0_begin (rai::transaction const &) = 0;
	virtual rai::store_iterator<rai::pending_key, rai::pending_info> pending_v0_end () = 0;
	virtual rai::store_iterator<rai::pending_key, rai::pending_info> pending_v1_begin (rai::transaction const &, rai::pending_key const &) = 0;
	virtual rai::store_iterator<rai::pending_key, rai::pending_info> pending_v1_begin (rai::transaction const &) = 0;
	virtual rai::store_iterator<rai::pending_key, rai::pending_info> pending_v1_end () = 0;
	virtual rai::store_iterator<rai::pending_key, rai::pending_info> pending_begin (rai::transaction const &, rai::pending_key const &) = 0;
	virtual rai::store_iterator<rai::pending_key, rai::pending_info> pending_begin (rai::transaction const &) = 0;
	virtual rai::store_iterator<rai::pending_key, rai::pending_info> pending_end () = 0;

	virtual void block_info_put (rai::transaction const &, rai::block_hash const &, rai::block_info const &) = 0;
	virtual void block_info_del (rai::transaction const &, rai::block_hash const &) = 0;
	virtual bool block_info_get (rai::transaction const &, rai::block_hash const &, rai::block_info &) = 0;
	virtual bool block_info_exists (rai::transaction const &, rai::block_hash const &) = 0;
	virtual rai::store_iterator<rai::block_hash, rai::block_info> block_info_begin (rai::transaction const &, rai::block_hash const &) = 0;
	virtual rai::store_iterator<rai::block_hash, rai::block_info> block_info_begin (rai::transaction const &) = 0;
	virtual rai::store_iterator<rai::block_hash, rai::block_info> block_info_end () = 0;
	virtual rai::uint128_t block_balance (rai::transaction const &, rai::block_hash const &) = 0;
	virtual rai::epoch block_version (rai::transaction const &, rai::block_hash const &) = 0;
	static size_t const block_info_max = 32;

	virtual rai::uint128_t representation_get (rai::transaction const &, rai::account const &) = 0;
	virtual void representation_put (rai::transaction const &, rai::account const &, rai::uint128_t const &) = 0;
	virtual void representation_add (rai::transaction const &, rai::account const &, rai::uint128_t const &) = 0;
	virtual rai::store_iterator<rai::account, rai::uint128_union> representation_begin (rai::transaction const &) = 0;
	virtual rai::store_iterator<rai::account, rai::uint128_union> representation_end () = 0;

	virtual void unchecked_clear (rai::transaction const &) = 0;
	virtual void unchecked_put (rai::transaction const &, rai::unchecked_key const &, std::shared_ptr<rai::block> const &) = 0;
	virtual void unchecked_put (rai::transaction const &, rai::block_hash const &, std::shared_ptr<rai::block> const &) = 0;
	virtual std::vector<std::shared_ptr<rai::block>> unchecked_get (rai::transaction const &, rai::block_hash const &) = 0;
	virtual bool unchecked_exists (rai::transaction const &, rai::unchecked_key const &) = 0;
	virtual void unchecked_del (rai::transaction const &, rai::unchecked_key const &) = 0;
	virtual rai::store_iterator<rai::unchecked_key, std::shared_ptr<rai::block>> unchecked_begin (rai::transaction const &) = 0;
	virtual rai::store_iterator<rai::unchecked_key, std::shared_ptr<rai::block>> unchecked_begin (rai::transaction const &, rai::unchecked_key const &) = 0;
	virtual rai::store_iterator<rai::unchecked_key, std::shared_ptr<rai::block>> unchecked_end () = 0;
	virtual size_t unchecked_count (rai::transaction const &) = 0;

	virtual void checksum_put (rai::transaction const &, uint64_t, uint8_t, rai::checksum const &) = 0;
	virtual bool checksum_get (rai::transaction const &, uint64_t, uint8_t, rai::checksum &) = 0;
	virtual void checksum_del (rai::transaction const &, uint64_t, uint8_t) = 0;

	// Return latest vote for an account from store
	virtual std::shared_ptr<rai::vote> vote_get (rai::transaction const &, rai::account const &) = 0;
	// Populate vote with the next sequence number
	virtual std::shared_ptr<rai::vote> vote_generate (rai::transaction const &, rai::account const &, rai::raw_key const &, std::shared_ptr<rai::block>) = 0;
	virtual std::shared_ptr<rai::vote> vote_generate (rai::transaction const &, rai::account const &, rai::raw_key const &, std::vector<rai::block_hash>) = 0;
	// Return either vote or the stored vote with a higher sequence number
	virtual std::shared_ptr<rai::vote> vote_max (rai::transaction const &, std::shared_ptr<rai::vote>) = 0;
	// Return latest vote for an account considering the vote cache
	virtual std::shared_ptr<rai::vote> vote_current (rai::transaction const &, rai::account const &) = 0;
	virtual void flush (rai::transaction const &) = 0;
	virtual rai::store_iterator<rai::account, std::shared_ptr<rai::vote>> vote_begin (rai::transaction const &) = 0;
	virtual rai::store_iterator<rai::account, std::shared_ptr<rai::vote>> vote_end () = 0;

	virtual void version_put (rai::transaction const &, int) = 0;
	virtual int version_get (rai::transaction const &) = 0;

	// Requires a write transaction
	virtual rai::raw_key get_node_id (rai::transaction const &) = 0;

	/** Deletes the node ID from the store */
	virtual void delete_node_id (rai::transaction const &) = 0;

	/** Start read-write transaction */
	virtual rai::transaction tx_begin_write () = 0;

	/** Start read-only transaction */
	virtual rai::transaction tx_begin_read () = 0;

	/**
	 * Start a read-only or read-write transaction
	 * @param write If true, start a read-write transaction
	 */
	virtual rai::transaction tx_begin (bool write = false) = 0;
};
}
