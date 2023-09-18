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
 * Manages final vote storage and iteration
 */
class final_vote_store
{
public:
	virtual bool put (nano::write_transaction const & transaction_a, nano::qualified_root const & root_a, nano::block_hash const & hash_a) = 0;
	virtual std::vector<nano::block_hash> get (nano::transaction const & transaction_a, nano::root const & root_a) = 0;
	virtual void del (nano::write_transaction const & transaction_a, nano::root const & root_a) = 0;
	virtual size_t count (nano::transaction const & transaction_a) const = 0;
	virtual void clear (nano::write_transaction const &, nano::root const &) = 0;
	virtual void clear (nano::write_transaction const &) = 0;
	virtual nano::store_iterator<nano::qualified_root, nano::block_hash> begin (nano::transaction const & transaction_a, nano::qualified_root const & root_a) const = 0;
	virtual nano::store_iterator<nano::qualified_root, nano::block_hash> begin (nano::transaction const & transaction_a) const = 0;
	virtual nano::store_iterator<nano::qualified_root, nano::block_hash> end () const = 0;
	virtual void for_each_par (std::function<void (nano::read_transaction const &, nano::store_iterator<nano::qualified_root, nano::block_hash>, nano::store_iterator<nano::qualified_root, nano::block_hash>)> const & action_a) const = 0;
};
} // namespace nano::store
