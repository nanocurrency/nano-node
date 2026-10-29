#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>

#include <cstddef>
#include <memory>
#include <vector>

namespace nano
{
/**
 * Keeps a key as random XOR shares in separate heap allocations, so that between uses it is not stored in one piece.
 * This only hides the key from reads of part of the process memory; whoever can read all of it can recombine the shares.
 */
class fan final
{
public:
	fan (nano::raw_key const & key, std::size_t count);
	void value (nano::raw_key & result) const;
	void value_set (nano::raw_key const & value);
	std::vector<std::unique_ptr<nano::raw_key>> values;

private:
	mutable nano::mutex mutex;
	void value_get (nano::raw_key & result) const;
};
}
