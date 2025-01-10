#pragma once

#include <celerix/lib/numbers.hpp>
#include <celerix/store/component.hpp>

#include <functional>

namespace celerix
{
class block_hash;
}
namespace celerix::store
{
/**
 * Manages version storage
 */
class version
{
public:
	virtual void put (store::write_transaction const &, int) = 0;
	virtual int get (store::transaction const &) const = 0;
};
} // namespace celerix::store
