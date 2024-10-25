#pragma once

#include <nano/lib/block_sideband.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/store/block_w_sideband.hpp>
#include <nano/store/component.hpp>
#include <nano/store/iterator.hpp>

#include <functional>
#include <optional>

namespace nano
{
class block;
class block_hash;
}
namespace nano::store
{
/**
 * Manages block storage and iteration
 */
class block
{
public:
	using iterator = store::iterator<nano::block_hash, block_w_sideband>;

public:
	virtual void put (store::write_transaction const &, nano::block_hash const &, nano::block const &) = 0;
	virtual void raw_put (store::write_transaction const &, std::vector<uint8_t> const &, nano::block_hash const &) = 0;
	virtual std::optional<nano::block_hash> successor (store::transaction const &, nano::block_hash const &) const = 0;
	virtual void successor_clear (store::write_transaction const &, nano::block_hash const &) = 0;
	virtual std::shared_ptr<nano::block> get (store::transaction const &, nano::block_hash const &) const = 0;
	virtual std::shared_ptr<nano::block> random (store::transaction const &) = 0;
	virtual void del (store::write_transaction const &, nano::block_hash const &) = 0;
	virtual bool exists (store::transaction const &, nano::block_hash const &) = 0;
	virtual uint64_t count (store::transaction const &) = 0;
	virtual iterator begin (store::transaction const &, nano::block_hash const &) const = 0;
	virtual iterator begin (store::transaction const &) const = 0;
	virtual iterator end (store::transaction const &) const = 0;
	virtual void for_each_par (std::function<void (store::read_transaction const &, iterator, iterator)> const & action_a) const = 0;
};
} // namespace nano::store
