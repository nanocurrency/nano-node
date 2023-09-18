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
 * Manages confirmation height storage and iteration
 */
class confirmation_height_store
{
public:
	virtual void put (nano::write_transaction const & transaction_a, nano::account const & account_a, nano::confirmation_height_info const & confirmation_height_info_a) = 0;

	/** Retrieves confirmation height info relating to an account.
	 *  The parameter confirmation_height_info_a is always written.
	 *  On error, the confirmation height and frontier hash are set to 0.
	 *  Ruturns true on error, false on success.
	 */
	virtual bool get (nano::transaction const & transaction_a, nano::account const & account_a, nano::confirmation_height_info & confirmation_height_info_a) = 0;
	std::optional<nano::confirmation_height_info> get (nano::transaction const & transaction_a, nano::account const & account_a);
	virtual bool exists (nano::transaction const & transaction_a, nano::account const & account_a) const = 0;
	virtual void del (nano::write_transaction const & transaction_a, nano::account const & account_a) = 0;
	virtual uint64_t count (nano::transaction const & transaction_a) = 0;
	virtual void clear (nano::write_transaction const &, nano::account const &) = 0;
	virtual void clear (nano::write_transaction const &) = 0;
	virtual nano::store_iterator<nano::account, nano::confirmation_height_info> begin (nano::transaction const & transaction_a, nano::account const & account_a) const = 0;
	virtual nano::store_iterator<nano::account, nano::confirmation_height_info> begin (nano::transaction const & transaction_a) const = 0;
	virtual nano::store_iterator<nano::account, nano::confirmation_height_info> end () const = 0;
	virtual void for_each_par (std::function<void (nano::read_transaction const &, nano::store_iterator<nano::account, nano::confirmation_height_info>, nano::store_iterator<nano::account, nano::confirmation_height_info>)> const &) const = 0;
};
} // namespace nano::store
