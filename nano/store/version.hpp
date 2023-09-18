#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/store/component.hpp>

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
 * Manages version storage
 */
class version_store
{
public:
	virtual void put (nano::write_transaction const &, int) = 0;
	virtual int get (nano::transaction const &) const = 0;
};

} // namespace nano::store
