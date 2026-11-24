#pragma once

#include <cstdint>

namespace nano
{
// Spreads a 64-bit value over the whole range, the finalizer of splitmix64: a bijection in which every output bit depends on every input bit, so values that differ in a few bits fall into different buckets
constexpr uint64_t mix64 (uint64_t value)
{
	value ^= value >> 30;
	value *= 0xbf58476d1ce4e5b9ULL;
	value ^= value >> 27;
	value *= 0x94d049bb133111ebULL;
	value ^= value >> 31;
	return value;
}

// Hash of an address for picking a bucket; addresses of objects allocated one after another share their low bits and differ in few others, so the address is mixed first
inline uint64_t pointer_hash (void const * pointer)
{
	return mix64 (reinterpret_cast<uintptr_t> (pointer));
}
}
