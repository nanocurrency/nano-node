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
 * Manages pending storage and iteration
 */
class pending_store
{
public:
	virtual void put (nano::write_transaction const &, nano::pending_key const &, nano::pending_info const &) = 0;
	virtual void del (nano::write_transaction const &, nano::pending_key const &) = 0;
	virtual bool get (nano::transaction const &, nano::pending_key const &, nano::pending_info &) = 0;
	virtual bool exists (nano::transaction const &, nano::pending_key const &) = 0;
	virtual bool any (nano::transaction const &, nano::account const &) = 0;
	virtual nano::store_iterator<nano::pending_key, nano::pending_info> begin (nano::transaction const &, nano::pending_key const &) const = 0;
	virtual nano::store_iterator<nano::pending_key, nano::pending_info> begin (nano::transaction const &) const = 0;
	virtual nano::store_iterator<nano::pending_key, nano::pending_info> end () const = 0;
	virtual void for_each_par (std::function<void (nano::read_transaction const &, nano::store_iterator<nano::pending_key, nano::pending_info>, nano::store_iterator<nano::pending_key, nano::pending_info>)> const & action_a) const = 0;
};
} // namespace nano::store
