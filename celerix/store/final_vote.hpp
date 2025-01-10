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
 * Manages final vote storage and iteration
 */
class final_vote
{
public:
	using iterator = typed_iterator<celerix::qualified_root, celerix::block_hash>;

public:
	virtual bool put (store::write_transaction const & transaction_a, celerix::qualified_root const & root_a, celerix::block_hash const & hash_a) = 0;
	virtual std::optional<celerix::block_hash> get (store::transaction const & transaction_a, celerix::qualified_root const & qualified_root_a) = 0;
	virtual void del (store::write_transaction const & transaction_a, celerix::qualified_root const & root_a) = 0;
	virtual size_t count (store::transaction const & transaction_a) const = 0;
	virtual void clear (store::write_transaction const &) = 0;
	virtual iterator begin (store::transaction const & transaction_a, celerix::qualified_root const & root_a) const = 0;
	virtual iterator begin (store::transaction const & transaction_a) const = 0;
	virtual iterator end (store::transaction const & transaction_a) const = 0;
	virtual void for_each_par (std::function<void (store::read_transaction const &, iterator, iterator)> const & action_a) const = 0;
};
} // namespace celerix::store
