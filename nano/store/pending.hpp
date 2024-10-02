#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/store/component.hpp>
#include <nano/store/iterator.hpp>

#include <functional>
#include <optional>

namespace nano
{
class block_hash;
class pending_info;
class pending_key;
}
namespace nano::store
{
/**
 * Manages pending storage and iteration
 */
class pending
{
public:
	using iterator = store::iterator<nano::pending_key, nano::pending_info>;

public:
	virtual void put (store::write_transaction const &, nano::pending_key const &, nano::pending_info const &) = 0;
	virtual void del (store::write_transaction const &, nano::pending_key const &) = 0;
	virtual std::optional<nano::pending_info> get (store::transaction const &, nano::pending_key const &) = 0;
	virtual bool exists (store::transaction const &, nano::pending_key const &) = 0;
	virtual bool any (store::transaction const &, nano::account const &) = 0;
	virtual store::iterator<nano::pending_key, nano::pending_info> begin (store::transaction const &, nano::pending_key const &) const = 0;
	virtual store::iterator<nano::pending_key, nano::pending_info> begin (store::transaction const &) const = 0;
	virtual store::iterator<nano::pending_key, nano::pending_info> end () const = 0;
	virtual void for_each_par (std::function<void (store::read_transaction const &, store::iterator<nano::pending_key, nano::pending_info>, store::iterator<nano::pending_key, nano::pending_info>)> const & action_a) const = 0;
};
} // namespace nano::store
