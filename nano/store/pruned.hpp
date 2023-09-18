#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/store/component.hpp>
#include <nano/store/iterator.hpp>

#include <functional>

namespace nano
{
class block_hash;
class read_transaction;
class transaction;
class write_transaction;
}
namespace nano
{
/**
 * Manages pruned storage and iteration
 */
class pruned_store
{
public:
	virtual void put (nano::write_transaction const & transaction_a, nano::block_hash const & hash_a) = 0;
	virtual void del (nano::write_transaction const & transaction_a, nano::block_hash const & hash_a) = 0;
	virtual bool exists (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const = 0;
	virtual nano::block_hash random (nano::transaction const & transaction_a) = 0;
	virtual size_t count (nano::transaction const & transaction_a) const = 0;
	virtual void clear (nano::write_transaction const &) = 0;
	virtual nano::store_iterator<nano::block_hash, std::nullptr_t> begin (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const = 0;
	virtual nano::store_iterator<nano::block_hash, std::nullptr_t> begin (nano::transaction const & transaction_a) const = 0;
	virtual nano::store_iterator<nano::block_hash, std::nullptr_t> end () const = 0;
	virtual void for_each_par (std::function<void (nano::read_transaction const &, nano::store_iterator<nano::block_hash, std::nullptr_t>, nano::store_iterator<nano::block_hash, std::nullptr_t>)> const & action_a) const = 0;
};
} // namespace nano::store
