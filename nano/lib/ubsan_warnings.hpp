#if defined(_WIN32)
#define __IGNORE_UBSAN_UINT_OVERFLOW__
#else
#define __IGNORE_UBSAN_UINT_OVERFLOW__ \
	__attribute__ ((no_sanitize ("unsigned-integer-overflow")))
#endif