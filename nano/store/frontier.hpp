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
namespace nano::store
{
/**
 * Manages frontier storage and iteration
 */
class frontier
{
public:
	virtual void put (store::write_transaction const &, nano::block_hash const &, nano::account const &) = 0;
	virtual nano::account get (store::transaction const &, nano::block_hash const &) const = 0;
	virtual void del (store::write_transaction const &, nano::block_hash const &) = 0;
	virtual iterator<nano::block_hash, nano::account> begin (store::transaction const &) const = 0;
	virtual iterator<nano::block_hash, nano::account> begin (store::transaction const &, nano::block_hash const &) const = 0;
	virtual iterator<nano::block_hash, nano::account> end () const = 0;
	virtual void for_each_par (std::function<void (store::read_transaction const &, store::iterator<nano::block_hash, nano::account>, store::iterator<nano::block_hash, nano::account>)> const & action_a) const = 0;
};
} // namespace nano::store
