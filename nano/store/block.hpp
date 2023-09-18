#pragma once

#include <nano/lib/blocks.hpp>
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
class block_w_sideband
{
public:
	std::shared_ptr<nano::block> block;
	nano::block_sideband sideband;
};
/**
 * Manages block storage and iteration
 */
class block_store
{
public:
	virtual void put (nano::write_transaction const &, nano::block_hash const &, nano::block const &) = 0;
	virtual void raw_put (nano::write_transaction const &, std::vector<uint8_t> const &, nano::block_hash const &) = 0;
	virtual nano::block_hash successor (nano::transaction const &, nano::block_hash const &) const = 0;
	virtual void successor_clear (nano::write_transaction const &, nano::block_hash const &) = 0;
	virtual std::shared_ptr<nano::block> get (nano::transaction const &, nano::block_hash const &) const = 0;
	virtual std::shared_ptr<nano::block> random (nano::transaction const &) = 0;
	virtual void del (nano::write_transaction const &, nano::block_hash const &) = 0;
	virtual bool exists (nano::transaction const &, nano::block_hash const &) = 0;
	virtual uint64_t count (nano::transaction const &) = 0;
	virtual nano::store_iterator<nano::block_hash, block_w_sideband> begin (nano::transaction const &, nano::block_hash const &) const = 0;
	virtual nano::store_iterator<nano::block_hash, block_w_sideband> begin (nano::transaction const &) const = 0;
	virtual nano::store_iterator<nano::block_hash, block_w_sideband> end () const = 0;
	virtual void for_each_par (std::function<void (nano::read_transaction const &, nano::store_iterator<nano::block_hash, block_w_sideband>, nano::store_iterator<nano::block_hash, block_w_sideband>)> const & action_a) const = 0;
};
} // namespace nano::store
