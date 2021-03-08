#include <nano/crypto_lib/secure_memory.hpp>

#ifdef _MSC_VER
#include <windows.h>
#endif
#if defined __STDC_LIB_EXT1__
#define __STDC_WANT_LIB_EXT1__ 1
#endif

/* for explicit_bzero() on glibc */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <string.h>

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

#if defined(__OpenBSD__)
#define HAVE_EXPLICIT_BZERO 1
#elif defined(__GLIBC__) && defined(__GLIBC_PREREQ)
#if __GLIBC_PREREQ(2, 25)
#define HAVE_EXPLICIT_BZERO 1
#endif
#endif

void NOT_OPTIMIZED nano::secure_wipe_memory (void * v, size_t n)
{
#if defined(_MSC_VER)
	SecureZeroMemory (v, n);
#elif defined memset_s
	memset_s (v, n, 0, n);
#elif defined(HAVE_EXPLICIT_BZERO)
	explicit_bzero (v, n);
#else
	static void * (*const volatile memset_sec) (void *, int, size_t) = &memset;
	memset_sec (v, 0, n);
#endif
}
