#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/store/component.hpp>
#include <nano/store/iterator.hpp>

#include <functional>

namespace nano
{
class block_hash;
}
namespace nano::store
{
/**
 * Manages pruned storage and iteration
 */
class pruned
{
public:
	using iterator = store::iterator<nano::block_hash, std::nullptr_t>;

public:
	virtual void put (store::write_transaction const & transaction_a, nano::block_hash const & hash_a) = 0;
	virtual void del (store::write_transaction const & transaction_a, nano::block_hash const & hash_a) = 0;
	virtual bool exists (store::transaction const & transaction_a, nano::block_hash const & hash_a) const = 0;
	virtual nano::block_hash random (store::transaction const & transaction_a) = 0;
	virtual size_t count (store::transaction const & transaction_a) const = 0;
	virtual void clear (store::write_transaction const &) = 0;
	virtual iterator begin (store::transaction const & transaction_a, nano::block_hash const & hash_a) const = 0;
	virtual iterator begin (store::transaction const & transaction_a) const = 0;
	virtual iterator end () const = 0;
	virtual void for_each_par (std::function<void (store::read_transaction const &, iterator, iterator)> const & action_a) const = 0;
};
} // namespace nano::store
