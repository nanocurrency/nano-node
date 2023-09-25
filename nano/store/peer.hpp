#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/store/component.hpp>
#include <nano/store/iterator.hpp>

#include <functional>

namespace nano
{
class block_hash;
}
namespace nano::store
{
/**
 * Manages peer storage and iteration
 */
class peer
{
public:
	virtual void put (store::write_transaction const & transaction_a, nano::endpoint_key const & endpoint_a) = 0;
	virtual void del (store::write_transaction const & transaction_a, nano::endpoint_key const & endpoint_a) = 0;
	virtual bool exists (store::transaction const & transaction_a, nano::endpoint_key const & endpoint_a) const = 0;
	virtual size_t count (store::transaction const & transaction_a) const = 0;
	virtual void clear (store::write_transaction const & transaction_a) = 0;
	virtual store::iterator<nano::endpoint_key, nano::no_value> begin (store::transaction const & transaction_a) const = 0;
	virtual store::iterator<nano::endpoint_key, nano::no_value> end () const = 0;
};
} // namespace nano::store
