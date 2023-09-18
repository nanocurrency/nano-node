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
 * Manages peer storage and iteration
 */
class peer_store
{
public:
	virtual void put (nano::write_transaction const & transaction_a, nano::endpoint_key const & endpoint_a) = 0;
	virtual void del (nano::write_transaction const & transaction_a, nano::endpoint_key const & endpoint_a) = 0;
	virtual bool exists (nano::transaction const & transaction_a, nano::endpoint_key const & endpoint_a) const = 0;
	virtual size_t count (nano::transaction const & transaction_a) const = 0;
	virtual void clear (nano::write_transaction const & transaction_a) = 0;
	virtual nano::store_iterator<nano::endpoint_key, nano::no_value> begin (nano::transaction const & transaction_a) const = 0;
	virtual nano::store_iterator<nano::endpoint_key, nano::no_value> end () const = 0;
};
} // namespace nano::store
