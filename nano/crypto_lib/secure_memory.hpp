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

/// Bytes below its caller that secure_wipe_stack clears, over twice what the deepest secret operation (argon2) uses; debug builds clear more, as unoptimized x86 BLAKE2b alone keeps a 26 KiB frame
#ifdef NDEBUG
std::size_t constexpr stack_wipe_size = 16 * 1024;
#else
std::size_t constexpr stack_wipe_size = 32 * 1024;
#endif

/// Zeroes the stack below the caller, clearing the secrets that functions it called left in their frames
void NOT_OPTIMIZED secure_wipe_stack ();
}