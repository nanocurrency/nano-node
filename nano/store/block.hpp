#pragma once

#include <nano/lib/block_sideband.hpp>
#include <nano/lib/blocks.hpp>
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
class block_w_sideband
{
public:
	std::shared_ptr<nano::block> block;
	nano::block_sideband sideband;
};
/**
 * Manages block storage and iteration
 */
class block
{
public:
	virtual void put (store::write_transaction const &, nano::block_hash const &, nano::block const &) = 0;
	virtual void raw_put (store::write_transaction const &, std::vector<uint8_t> const &, nano::block_hash const &) = 0;
	virtual nano::block_hash successor (store::transaction const &, nano::block_hash const &) const = 0;
	virtual void successor_clear (store::write_transaction const &, nano::block_hash const &) = 0;
	virtual std::shared_ptr<nano::block> get (store::transaction const &, nano::block_hash const &) const = 0;
	virtual std::shared_ptr<nano::block> random (store::transaction const &) = 0;
	virtual void del (store::write_transaction const &, nano::block_hash const &) = 0;
	virtual bool exists (store::transaction const &, nano::block_hash const &) = 0;
	virtual uint64_t count (store::transaction const &) = 0;
	virtual iterator<nano::block_hash, block_w_sideband> begin (store::transaction const &, nano::block_hash const &) const = 0;
	virtual iterator<nano::block_hash, block_w_sideband> begin (store::transaction const &) const = 0;
	virtual iterator<nano::block_hash, block_w_sideband> end () const = 0;
	virtual void for_each_par (std::function<void (store::read_transaction const &, iterator<nano::block_hash, block_w_sideband>, iterator<nano::block_hash, block_w_sideband>)> const & action_a) const = 0;
};
} // namespace nano::store
