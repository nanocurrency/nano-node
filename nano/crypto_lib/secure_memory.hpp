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
void NOT_OPTIMIZED secure_wipe_memory (void * v, size_t n);
}