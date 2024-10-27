#pragma once

#include <nano/lib/block_sideband.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/store/block_w_sideband.hpp>
#include <nano/store/component.hpp>
#include <nano/store/typed_iterator.hpp>

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
	using iterator = typed_iterator<nano::block_hash, block_w_sideband>;

public:
	virtual void put (write_transaction const & tx, nano::block_hash const &, nano::block const &) = 0;
	virtual void raw_put (write_transaction const & tx, std::vector<uint8_t> const &, nano::block_hash const &) = 0;
	virtual std::optional<nano::block_hash> successor (transaction const & tx, nano::block_hash const &) const = 0;
	virtual void successor_clear (write_transaction const & tx, nano::block_hash const &) = 0;
	virtual std::shared_ptr<nano::block> get (transaction const & tx, nano::block_hash const &) const = 0;
	virtual std::shared_ptr<nano::block> random (transaction const & tx) = 0;
	virtual void del (write_transaction const & tx, nano::block_hash const &) = 0;
	virtual bool exists (transaction const & tx, nano::block_hash const &) = 0;
	virtual uint64_t count (transaction const & tx) = 0;
	virtual iterator begin (transaction const & tx, nano::block_hash const &) const = 0;
	virtual iterator begin (transaction const & tx) const = 0;
	virtual iterator end (transaction const & tx) const = 0;
	virtual void for_each_par (std::function<void (read_transaction const & tx, iterator, iterator)> const & action_a) const = 0;
};
} // namespace nano::store
