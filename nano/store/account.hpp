#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/store/component.hpp>
#include <nano/store/db_val_impl.hpp>
#include <nano/store/iterator.hpp>

#include <functional>

namespace nano
{
class account_info;
class block_hash;
}
namespace nano::store
{
/**
 * Manages account storage and iteration
 */
class account
{
public:
	using iterator = store::iterator<nano::account, nano::account_info>;

public:
	virtual void put (store::write_transaction const &, nano::account const &, nano::account_info const &) = 0;
	virtual bool get (store::transaction const &, nano::account const &, nano::account_info &) = 0;
	std::optional<nano::account_info> get (store::transaction const &, nano::account const &);
	virtual void del (store::write_transaction const &, nano::account const &) = 0;
	virtual bool exists (store::transaction const &, nano::account const &) = 0;
	virtual size_t count (store::transaction const &) = 0;
	virtual store::iterator<nano::account, nano::account_info> begin (store::transaction const &, nano::account const &) const = 0;
	virtual store::iterator<nano::account, nano::account_info> begin (store::transaction const &) const = 0;
	virtual store::iterator<nano::account, nano::account_info> rbegin (store::transaction const &) const = 0;
	virtual store::iterator<nano::account, nano::account_info> end () const = 0;
	virtual void for_each_par (std::function<void (store::read_transaction const &, store::iterator<nano::account, nano::account_info>, store::iterator<nano::account, nano::account_info>)> const &) const = 0;
};
} // namespace nano::store
