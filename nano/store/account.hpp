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
 * Manages account storage and iteration
 */
class account
{
public:
	virtual void put (store::write_transaction const &, nano::account const &, nano::account_info const &) = 0;
	virtual bool get (store::transaction const &, nano::account const &, nano::account_info &) = 0;
	std::optional<nano::account_info> get (store::transaction const &, nano::account const &);
	virtual void del (store::write_transaction const &, nano::account const &) = 0;
	virtual bool exists (store::transaction const &, nano::account const &) = 0;
	virtual size_t count (store::transaction const &) = 0;
	virtual iterator<nano::account, nano::account_info> begin (store::transaction const &, nano::account const &) const = 0;
	virtual iterator<nano::account, nano::account_info> begin (store::transaction const &) const = 0;
	virtual iterator<nano::account, nano::account_info> rbegin (store::transaction const &) const = 0;
	virtual iterator<nano::account, nano::account_info> end () const = 0;
	virtual void for_each_par (std::function<void (store::read_transaction const &, iterator<nano::account, nano::account_info>, iterator<nano::account, nano::account_info>)> const &) const = 0;
};
} // namespace nano::store
