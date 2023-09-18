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
 * Manages online weight storage and iteration
 */
class online_weight_store
{
public:
	virtual void put (nano::write_transaction const &, uint64_t, nano::amount const &) = 0;
	virtual void del (nano::write_transaction const &, uint64_t) = 0;
	virtual nano::store_iterator<uint64_t, nano::amount> begin (nano::transaction const &) const = 0;
	virtual nano::store_iterator<uint64_t, nano::amount> rbegin (nano::transaction const &) const = 0;
	virtual nano::store_iterator<uint64_t, nano::amount> end () const = 0;
	virtual size_t count (nano::transaction const &) const = 0;
	virtual void clear (nano::write_transaction const &) = 0;
};
} // namespace nano::store
