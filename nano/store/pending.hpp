#pragma once

#include <celerix/lib/numbers.hpp>
#include <celerix/store/component.hpp>
#include <celerix/store/typed_iterator.hpp>

#include <functional>
#include <optional>

namespace celerix
{
class block_hash;
class pending_info;
class pending_key;
}
namespace celerix::store
{
/**
 * Manages pending storage and iteration
 */
class pending
{
public:
	using iterator = typed_iterator<celerix::pending_key, celerix::pending_info>;

public:
	virtual void put (store::write_transaction const &, celerix::pending_key const &, celerix::pending_info const &) = 0;
	virtual void del (store::write_transaction const &, celerix::pending_key const &) = 0;
	virtual std::optional<celerix::pending_info> get (store::transaction const &, celerix::pending_key const &) = 0;
	virtual bool exists (store::transaction const &, celerix::pending_key const &) = 0;
	virtual bool any (store::transaction const &, celerix::account const &) = 0;
	virtual iterator begin (store::transaction const &, celerix::pending_key const &) const = 0;
	virtual iterator begin (store::transaction const &) const = 0;
	virtual iterator end (store::transaction const & transaction_a) const = 0;
	virtual void for_each_par (std::function<void (store::read_transaction const &, iterator, iterator)> const & action_a) const = 0;
};
} // namespace celerix::store
