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
 * Manages frontier storage and iteration
 */
class frontier_store
{
public:
	virtual void put (nano::write_transaction const &, nano::block_hash const &, nano::account const &) = 0;
	virtual nano::account get (nano::transaction const &, nano::block_hash const &) const = 0;
	virtual void del (nano::write_transaction const &, nano::block_hash const &) = 0;
	virtual nano::store_iterator<nano::block_hash, nano::account> begin (nano::transaction const &) const = 0;
	virtual nano::store_iterator<nano::block_hash, nano::account> begin (nano::transaction const &, nano::block_hash const &) const = 0;
	virtual nano::store_iterator<nano::block_hash, nano::account> end () const = 0;
	virtual void for_each_par (std::function<void (nano::read_transaction const &, nano::store_iterator<nano::block_hash, nano::account>, nano::store_iterator<nano::block_hash, nano::account>)> const & action_a) const = 0;
};
} // namespace nano::store
