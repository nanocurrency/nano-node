#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/store/component.hpp>
#include <nano/store/typed_iterator.hpp>

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
	using iterator = typed_iterator<nano::endpoint_key, nano::millis_t>;

public:
	/// Returns true if the peer was inserted, false if it was already in the container
	virtual void put (store::write_transaction const &, nano::endpoint_key const & endpoint, nano::millis_t timestamp) = 0;
	virtual nano::millis_t get (store::transaction const &, nano::endpoint_key const & endpoint) const = 0;
	virtual void del (store::write_transaction const &, nano::endpoint_key const & endpoint) = 0;
	virtual bool exists (store::transaction const &, nano::endpoint_key const & endpoint) const = 0;
	virtual size_t count (store::transaction const &) const = 0;
	virtual void clear (store::write_transaction const &) = 0;
	virtual iterator begin (store::transaction const &) const = 0;
	virtual iterator end (store::transaction const & transaction_a) const = 0;
};
} // namespace nano::store
