#pragma once

#include <cstddef>

#if defined(__clang__)
#if __has_attribute(optnone)
#define NOT_OPTIMIZED __attribute__ ((optnone))
#endif
#elif defined(__GNUC__)
#define NOT_OPTIMIZED __attribute__ ((optimize ("O0")))
#endif
#ifndef NOT_OPTIMIZED
#define NOT_OPTIMIZED
#endif

namespace nano
{
/// Zeroes n bytes at v in a way the compiler cannot optimize away
void NOT_OPTIMIZED secure_wipe_memory (void * v, size_t n);

/// Bytes below its caller that secure_wipe_stack clears, over twice what the deepest secret operation (argon2) uses in an optimized build
std::size_t constexpr stack_wipe_size = 16 * 1024;

/// Zeroes the stack below the caller, clearing the secrets that functions it called left in their frames
/// Best effort: frames deeper than stack_wipe_size, which unoptimized builds can have, keep what they hold
void NOT_OPTIMIZED secure_wipe_stack ();
}