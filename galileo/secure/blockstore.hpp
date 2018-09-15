#pragma once

#include <galileo/secure/common.hpp>

namespace galileo
{
template <typename T, typename U>
class store_iterator_impl
{
public:
	virtual ~store_iterator_impl () = default;
	virtual galileo::store_iterator_impl<T, U> & operator++ () = 0;
	virtual bool operator== (galileo::store_iterator_impl<T, U> const & other_a) const = 0;
	virtual void next_dup () = 0;
	virtual bool is_end_sentinal () const = 0;
	virtual void fill (std::pair<T, U> &) const = 0;
	galileo::store_iterator_impl<T, U> & operator= (galileo::store_iterator_impl<T, U> const &) = delete;
	bool operator== (galileo::store_iterator_impl<T, U> const * other_a) const
	{
		return (other_a != nullptr && *this == *other_a) || (other_a == nullptr && is_end_sentinal ());
	}
	bool operator!= (galileo::store_iterator_impl<T, U> const & other_a) const
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
	store_iterator (std::unique_ptr<galileo::store_iterator_impl<T, U>> impl_a) :
	impl (std::move (impl_a))
	{
		impl->fill (current);
	}
	store_iterator (galileo::store_iterator<T, U> && other_a) :
	current (std::move (other_a.current)),
	impl (std::move (other_a.impl))
	{
	}
	galileo::store_iterator<T, U> & operator++ ()
	{
		++*impl;
		impl->fill (current);
		return *this;
	}
	galileo::store_iterator<T, U> & operator= (galileo::store_iterator<T, U> && other_a)
	{
		impl = std::move (other_a.impl);
		current = std::move (other_a.current);
		return *this;
	}
	galileo::store_iterator<T, U> & operator= (galileo::store_iterator<T, U> const &) = delete;
	std::pair<T, U> * operator-> ()
	{
		return &current;
	}
	bool operator== (galileo::store_iterator<T, U> const & other_a) const
	{
		return (impl == nullptr && other_a.impl == nullptr) || (impl != nullptr && *impl == other_a.impl.get ()) || (other_a.impl != nullptr && *other_a.impl == impl.get ());
	}
	bool operator!= (galileo::store_iterator<T, U> const & other_a) const
	{
		return !(*this == other_a);
	}
	void next_dup ()
	{
		impl->next_dup ();
	}

private:
	std::pair<T, U> current;
	std::unique_ptr<galileo::store_iterator_impl<T, U>> impl;
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
	std::unique_ptr<galileo::transaction_impl> impl;
};

/**
 * Manages block storage and iteration
 */
class block_store
{
public:
	virtual ~block_store () = default;
	virtual void initialize (galileo::transaction const &, galileo::genesis const &) = 0;
	virtual void block_put (galileo::transaction const &, galileo::block_hash const &, galileo::block const &, galileo::block_hash const & = galileo::block_hash (0), galileo::epoch version = galileo::epoch::epoch_0) = 0;
	virtual galileo::block_hash block_successor (galileo::transaction const &, galileo::block_hash const &) = 0;
	virtual void block_successor_clear (galileo::transaction const &, galileo::block_hash const &) = 0;
	virtual std::unique_ptr<galileo::block> block_get (galileo::transaction const &, galileo::block_hash const &) = 0;
	virtual std::unique_ptr<galileo::block> block_random (galileo::transaction const &) = 0;
	virtual void block_del (galileo::transaction const &, galileo::block_hash const &) = 0;
	virtual bool block_exists (galileo::transaction const &, galileo::block_hash const &) = 0;
	virtual galileo::block_counts block_count (galileo::transaction const &) = 0;
	virtual bool root_exists (galileo::transaction const &, galileo::uint256_union const &) = 0;

	virtual void frontier_put (galileo::transaction const &, galileo::block_hash const &, galileo::account const &) = 0;
	virtual galileo::account frontier_get (galileo::transaction const &, galileo::block_hash const &) = 0;
	virtual void frontier_del (galileo::transaction const &, galileo::block_hash const &) = 0;

	virtual void account_put (galileo::transaction const &, galileo::account const &, galileo::account_info const &) = 0;
	virtual bool account_get (galileo::transaction const &, galileo::account const &, galileo::account_info &) = 0;
	virtual void account_del (galileo::transaction const &, galileo::account const &) = 0;
	virtual bool account_exists (galileo::transaction const &, galileo::account const &) = 0;
	virtual size_t account_count (galileo::transaction const &) = 0;
	virtual galileo::store_iterator<galileo::account, galileo::account_info> latest_v0_begin (galileo::transaction const &, galileo::account const &) = 0;
	virtual galileo::store_iterator<galileo::account, galileo::account_info> latest_v0_begin (galileo::transaction const &) = 0;
	virtual galileo::store_iterator<galileo::account, galileo::account_info> latest_v0_end () = 0;
	virtual galileo::store_iterator<galileo::account, galileo::account_info> latest_v1_begin (galileo::transaction const &, galileo::account const &) = 0;
	virtual galileo::store_iterator<galileo::account, galileo::account_info> latest_v1_begin (galileo::transaction const &) = 0;
	virtual galileo::store_iterator<galileo::account, galileo::account_info> latest_v1_end () = 0;
	virtual galileo::store_iterator<galileo::account, galileo::account_info> latest_begin (galileo::transaction const &, galileo::account const &) = 0;
	virtual galileo::store_iterator<galileo::account, galileo::account_info> latest_begin (galileo::transaction const &) = 0;
	virtual galileo::store_iterator<galileo::account, galileo::account_info> latest_end () = 0;

	virtual void pending_put (galileo::transaction const &, galileo::pending_key const &, galileo::pending_info const &) = 0;
	virtual void pending_del (galileo::transaction const &, galileo::pending_key const &) = 0;
	virtual bool pending_get (galileo::transaction const &, galileo::pending_key const &, galileo::pending_info &) = 0;
	virtual bool pending_exists (galileo::transaction const &, galileo::pending_key const &) = 0;
	virtual galileo::store_iterator<galileo::pending_key, galileo::pending_info> pending_v0_begin (galileo::transaction const &, galileo::pending_key const &) = 0;
	virtual galileo::store_iterator<galileo::pending_key, galileo::pending_info> pending_v0_begin (galileo::transaction const &) = 0;
	virtual galileo::store_iterator<galileo::pending_key, galileo::pending_info> pending_v0_end () = 0;
	virtual galileo::store_iterator<galileo::pending_key, galileo::pending_info> pending_v1_begin (galileo::transaction const &, galileo::pending_key const &) = 0;
	virtual galileo::store_iterator<galileo::pending_key, galileo::pending_info> pending_v1_begin (galileo::transaction const &) = 0;
	virtual galileo::store_iterator<galileo::pending_key, galileo::pending_info> pending_v1_end () = 0;
	virtual galileo::store_iterator<galileo::pending_key, galileo::pending_info> pending_begin (galileo::transaction const &, galileo::pending_key const &) = 0;
	virtual galileo::store_iterator<galileo::pending_key, galileo::pending_info> pending_begin (galileo::transaction const &) = 0;
	virtual galileo::store_iterator<galileo::pending_key, galileo::pending_info> pending_end () = 0;

	virtual void block_info_put (galileo::transaction const &, galileo::block_hash const &, galileo::block_info const &) = 0;
	virtual void block_info_del (galileo::transaction const &, galileo::block_hash const &) = 0;
	virtual bool block_info_get (galileo::transaction const &, galileo::block_hash const &, galileo::block_info &) = 0;
	virtual bool block_info_exists (galileo::transaction const &, galileo::block_hash const &) = 0;
	virtual galileo::store_iterator<galileo::block_hash, galileo::block_info> block_info_begin (galileo::transaction const &, galileo::block_hash const &) = 0;
	virtual galileo::store_iterator<galileo::block_hash, galileo::block_info> block_info_begin (galileo::transaction const &) = 0;
	virtual galileo::store_iterator<galileo::block_hash, galileo::block_info> block_info_end () = 0;
	virtual galileo::uint128_t block_balance (galileo::transaction const &, galileo::block_hash const &) = 0;
	virtual galileo::epoch block_version (galileo::transaction const &, galileo::block_hash const &) = 0;
	static size_t const block_info_max = 32;

	virtual galileo::uint128_t representation_get (galileo::transaction const &, galileo::account const &) = 0;
	virtual void representation_put (galileo::transaction const &, galileo::account const &, galileo::uint128_t const &) = 0;
	virtual void representation_add (galileo::transaction const &, galileo::account const &, galileo::uint128_t const &) = 0;
	virtual galileo::store_iterator<galileo::account, galileo::uint128_union> representation_begin (galileo::transaction const &) = 0;
	virtual galileo::store_iterator<galileo::account, galileo::uint128_union> representation_end () = 0;

	virtual void unchecked_clear (galileo::transaction const &) = 0;
	virtual void unchecked_put (galileo::transaction const &, galileo::block_hash const &, std::shared_ptr<galileo::block> const &) = 0;
	virtual std::vector<std::shared_ptr<galileo::block>> unchecked_get (galileo::transaction const &, galileo::block_hash const &) = 0;
	virtual void unchecked_del (galileo::transaction const &, galileo::block_hash const &, std::shared_ptr<galileo::block>) = 0;
	virtual galileo::store_iterator<galileo::block_hash, std::shared_ptr<galileo::block>> unchecked_begin (galileo::transaction const &) = 0;
	virtual galileo::store_iterator<galileo::block_hash, std::shared_ptr<galileo::block>> unchecked_begin (galileo::transaction const &, galileo::block_hash const &) = 0;
	virtual galileo::store_iterator<galileo::block_hash, std::shared_ptr<galileo::block>> unchecked_end () = 0;
	virtual size_t unchecked_count (galileo::transaction const &) = 0;

	virtual void checksum_put (galileo::transaction const &, uint64_t, uint8_t, galileo::checksum const &) = 0;
	virtual bool checksum_get (galileo::transaction const &, uint64_t, uint8_t, galileo::checksum &) = 0;
	virtual void checksum_del (galileo::transaction const &, uint64_t, uint8_t) = 0;

	// Return latest vote for an account from store
	virtual std::shared_ptr<galileo::vote> vote_get (galileo::transaction const &, galileo::account const &) = 0;
	// Populate vote with the next sequence number
	virtual std::shared_ptr<galileo::vote> vote_generate (galileo::transaction const &, galileo::account const &, galileo::raw_key const &, std::shared_ptr<galileo::block>) = 0;
	virtual std::shared_ptr<galileo::vote> vote_generate (galileo::transaction const &, galileo::account const &, galileo::raw_key const &, std::vector<galileo::block_hash>) = 0;
	// Return either vote or the stored vote with a higher sequence number
	virtual std::shared_ptr<galileo::vote> vote_max (galileo::transaction const &, std::shared_ptr<galileo::vote>) = 0;
	// Return latest vote for an account considering the vote cache
	virtual std::shared_ptr<galileo::vote> vote_current (galileo::transaction const &, galileo::account const &) = 0;
	virtual void flush (galileo::transaction const &) = 0;
	virtual galileo::store_iterator<galileo::account, std::shared_ptr<galileo::vote>> vote_begin (galileo::transaction const &) = 0;
	virtual galileo::store_iterator<galileo::account, std::shared_ptr<galileo::vote>> vote_end () = 0;

	virtual void version_put (galileo::transaction const &, int) = 0;
	virtual int version_get (galileo::transaction const &) = 0;

	// Requires a write transaction
	virtual galileo::raw_key get_node_id (galileo::transaction const &) = 0;

	/** Deletes the node ID from the store */
	virtual void delete_node_id (galileo::transaction const &) = 0;

	/** Start read-write transaction */
	virtual galileo::transaction tx_begin_write () = 0;

	/** Start read-only transaction */
	virtual galileo::transaction tx_begin_read () = 0;

	/**
	 * Start a read-only or read-write transaction
	 * @param write If true, start a read-write transaction
	 */
	virtual galileo::transaction tx_begin (bool write = false) = 0;
};
}
