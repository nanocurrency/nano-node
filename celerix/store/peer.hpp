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
 * Manages peer storage and iteration
 */
class peer
{
public:
	using iterator = typed_iterator<celerix::endpoint_key, celerix::millis_t>;

public:
	/// Returns true if the peer was inserted, false if it was already in the container
	virtual void put (store::write_transaction const &, celerix::endpoint_key const & endpoint, celerix::millis_t timestamp) = 0;
	virtual celerix::millis_t get (store::transaction const &, celerix::endpoint_key const & endpoint) const = 0;
	virtual void del (store::write_transaction const &, celerix::endpoint_key const & endpoint) = 0;
	virtual bool exists (store::transaction const &, celerix::endpoint_key const & endpoint) const = 0;
	virtual size_t count (store::transaction const &) const = 0;
	virtual void clear (store::write_transaction const &) = 0;
	virtual iterator begin (store::transaction const &) const = 0;
	virtual iterator end (store::transaction const & transaction_a) const = 0;
};
} // namespace celerix::store
