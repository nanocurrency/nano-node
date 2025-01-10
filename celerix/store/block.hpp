#pragma once

#include <celerix/lib/block_sideband.hpp>
#include <celerix/lib/numbers.hpp>
#include <celerix/store/block_w_sideband.hpp>
#include <celerix/store/component.hpp>
#include <celerix/store/typed_iterator.hpp>

#include <functional>
#include <optional>

namespace celerix
{
class block;
class block_hash;
}
namespace celerix::store
{
/**
 * Manages block storage and iteration
 */
class block
{
public:
	using iterator = typed_iterator<celerix::block_hash, block_w_sideband>;

public:
	virtual void put (write_transaction const & tx, celerix::block_hash const &, celerix::block const &) = 0;
	virtual void raw_put (write_transaction const & tx, std::vector<uint8_t> const &, celerix::block_hash const &) = 0;
	virtual std::optional<celerix::block_hash> successor (transaction const & tx, celerix::block_hash const &) const = 0;
	virtual void successor_clear (write_transaction const & tx, celerix::block_hash const &) = 0;
	virtual std::shared_ptr<celerix::block> get (transaction const & tx, celerix::block_hash const &) const = 0;
	virtual void del (write_transaction const & tx, celerix::block_hash const &) = 0;
	virtual bool exists (transaction const & tx, celerix::block_hash const &) = 0;
	virtual uint64_t count (transaction const & tx) = 0;
	virtual iterator begin (transaction const & tx, celerix::block_hash const &) const = 0;
	virtual iterator begin (transaction const & tx) const = 0;
	virtual iterator end (transaction const & tx) const = 0;
	virtual void for_each_par (std::function<void (read_transaction const & tx, iterator, iterator)> const & action_a) const = 0;
};
} // namespace celerix::store
