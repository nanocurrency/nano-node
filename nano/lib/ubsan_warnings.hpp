#if defined(NANO_ASAN_ENABLED)
#if defined(__clang__)
#if __has_feature(memory_sanitizer)
#define __IGNORE_ASAN_WARNINGS__ \
	__attribute__ ((no_sanitize_memory))
#else
#define __IGNORE_ASAN_WARNINGS__
#endif
#elif defined(__GNUC__)
#define __IGNORE_ASAN_WARNINGS__ \
	__attribute__ ((no_sanitize_memory))
#else
#define __IGNORE_ASAN_WARNINGS__
#endif
#else
#define __IGNORE_ASAN_WARNINGS__
#endif