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
 * Manages pruned storage and iteration
 */
class pruned
{
public:
	using iterator = typed_iterator<celerix::block_hash, std::nullptr_t>;

public:
	virtual void put (store::write_transaction const & transaction_a, celerix::block_hash const & hash_a) = 0;
	virtual void del (store::write_transaction const & transaction_a, celerix::block_hash const & hash_a) = 0;
	virtual bool exists (store::transaction const & transaction_a, celerix::block_hash const & hash_a) const = 0;
	virtual celerix::block_hash random (store::transaction const & transaction_a) = 0;
	virtual size_t count (store::transaction const & transaction_a) const = 0;
	virtual void clear (store::write_transaction const &) = 0;
	virtual iterator begin (store::transaction const & transaction_a, celerix::block_hash const & hash_a) const = 0;
	virtual iterator begin (store::transaction const & transaction_a) const = 0;
	virtual iterator end (store::transaction const & transaction_a) const = 0;
	virtual void for_each_par (std::function<void (store::read_transaction const &, iterator, iterator)> const & action_a) const = 0;
};
} // namespace celerix::store
