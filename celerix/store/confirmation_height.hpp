#pragma once

#include <celerix/lib/numbers.hpp>
#include <celerix/store/component.hpp>
#include <celerix/store/typed_iterator.hpp>

#include <functional>

namespace celerix
{
class block_hash;
}
namespace celerix::store
{
/**
 * Manages confirmation height storage and iteration
 */
class confirmation_height
{
public:
	using iterator = typed_iterator<celerix::account, celerix::confirmation_height_info>;

public:
	virtual void put (store::write_transaction const & transaction_a, celerix::account const & account_a, celerix::confirmation_height_info const & confirmation_height_info_a) = 0;

	/** Retrieves confirmation height info relating to an account.
	 *  The parameter confirmation_height_info_a is always written.
	 *  On error, the confirmation height and frontier hash are set to 0.
	 *  Ruturns true on error, false on success.
	 */
	virtual bool get (store::transaction const & transaction_a, celerix::account const & account_a, celerix::confirmation_height_info & confirmation_height_info_a) = 0;
	std::optional<celerix::confirmation_height_info> get (store::transaction const & transaction_a, celerix::account const & account_a);
	virtual bool exists (store::transaction const & transaction_a, celerix::account const & account_a) const = 0;
	virtual void del (store::write_transaction const & transaction_a, celerix::account const & account_a) = 0;
	virtual uint64_t count (store::transaction const & transaction_a) = 0;
	virtual void clear (store::write_transaction const &, celerix::account const &) = 0;
	virtual void clear (store::write_transaction const &) = 0;
	virtual iterator begin (store::transaction const & transaction_a, celerix::account const & account_a) const = 0;
	virtual iterator begin (store::transaction const & transaction_a) const = 0;
	virtual iterator end (store::transaction const & transaction_a) const = 0;
	virtual void for_each_par (std::function<void (store::read_transaction const &, iterator, iterator)> const &) const = 0;
};
} // namespace celerix::store
