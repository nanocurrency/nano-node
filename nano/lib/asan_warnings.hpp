#if defined(__clang__)
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define ATTRIBUTE_NO_SANITIZE_UINT_OVERFLOW __attribute__ ((no_sanitize ("unsigned-integer-overflow")))
#else
#define ATTRIBUTE_NO_SANITIZE_UINT_OVERFLOW
#endif
#else
#define ATTRIBUTE_NO_SANITIZE_UINT_OVERFLOW
#endif
#else
#define ATTRIBUTE_NO_SANITIZE_UINT_OVERFLOW
#endif
