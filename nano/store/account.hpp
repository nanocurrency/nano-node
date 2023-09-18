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
 * Manages account storage and iteration
 */
class account_store
{
public:
	virtual void put (nano::write_transaction const &, nano::account const &, nano::account_info const &) = 0;
	virtual bool get (nano::transaction const &, nano::account const &, nano::account_info &) = 0;
	std::optional<nano::account_info> get (nano::transaction const &, nano::account const &);
	virtual void del (nano::write_transaction const &, nano::account const &) = 0;
	virtual bool exists (nano::transaction const &, nano::account const &) = 0;
	virtual size_t count (nano::transaction const &) = 0;
	virtual nano::store_iterator<nano::account, nano::account_info> begin (nano::transaction const &, nano::account const &) const = 0;
	virtual nano::store_iterator<nano::account, nano::account_info> begin (nano::transaction const &) const = 0;
	virtual nano::store_iterator<nano::account, nano::account_info> rbegin (nano::transaction const &) const = 0;
	virtual nano::store_iterator<nano::account, nano::account_info> end () const = 0;
	virtual void for_each_par (std::function<void (nano::read_transaction const &, nano::store_iterator<nano::account, nano::account_info>, nano::store_iterator<nano::account, nano::account_info>)> const &) const = 0;
};
} // namespace nano::store
