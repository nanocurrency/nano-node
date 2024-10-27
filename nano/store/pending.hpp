#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/store/component.hpp>
#include <nano/store/typed_iterator.hpp>

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
	using iterator = typed_iterator<nano::pending_key, nano::pending_info>;

public:
	virtual void put (store::write_transaction const &, nano::pending_key const &, nano::pending_info const &) = 0;
	virtual void del (store::write_transaction const &, nano::pending_key const &) = 0;
	virtual std::optional<nano::pending_info> get (store::transaction const &, nano::pending_key const &) = 0;
	virtual bool exists (store::transaction const &, nano::pending_key const &) = 0;
	virtual bool any (store::transaction const &, nano::account const &) = 0;
	virtual iterator begin (store::transaction const &, nano::pending_key const &) const = 0;
	virtual iterator begin (store::transaction const &) const = 0;
	virtual iterator end (store::transaction const & transaction_a) const = 0;
	virtual void for_each_par (std::function<void (store::read_transaction const &, iterator, iterator)> const & action_a) const = 0;
};
} // namespace nano::store
