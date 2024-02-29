#pragma once

#include <nano/lib/numbers.hpp>

namespace nano::store
{
class transaction;
class write_transaction;
}
namespace nano::store
{
/**
 * Manages block successor storage
 */
class successor
{
public:
	virtual void put (store::write_transaction const &, nano::block_hash const &, nano::block_hash const &) = 0;
	virtual nano::block_hash get (store::transaction const &, nano::block_hash const &) const = 0;
	virtual void del (store::write_transaction const &, nano::block_hash const &) = 0;
};
} // namespace nano::store
