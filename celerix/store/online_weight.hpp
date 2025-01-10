#pragma once

#include <celerix/lib/numbers.hpp>
#include <celerix/store/component.hpp>
#include <celerix/store/reverse_iterator.hpp>
#include <celerix/store/typed_iterator.hpp>

#include <functional>

namespace celerix
{
class block_hash;
}
namespace celerix::store
{
/**
 * Manages online weight storage and iteration
 */
class online_weight
{
public:
	using iterator = typed_iterator<uint64_t, celerix::amount>;
	using reverse_iterator = store::reverse_iterator<iterator>;

public:
	virtual void put (store::write_transaction const &, uint64_t, celerix::amount const &) = 0;
	virtual void del (store::write_transaction const &, uint64_t) = 0;
	virtual iterator begin (store::transaction const &) const = 0;
	reverse_iterator rbegin (store::transaction const &) const;
	reverse_iterator rend (store::transaction const &) const;
	virtual iterator end (store::transaction const & transaction_a) const = 0;
	virtual size_t count (store::transaction const &) const = 0;
	virtual void clear (store::write_transaction const &) = 0;
};
} // namespace celerix::store
