#pragma once

#include <cstdint>
#include <string>

/**
 * A traits class to allow us to use uint8_t as a char type for streams
 * Using `char_traits<uint8_t>` directly is not specified by the standard, is deprecated and scheduled for removal
 * Based on implementation from clang
 */
struct uint8_char_traits
{
	using char_type = uint8_t;
	using int_type = std::char_traits<char>::int_type;
	using off_type = std::char_traits<char>::off_type;
	using pos_type = std::char_traits<char>::pos_type;
	using state_type = std::char_traits<char>::state_type;

	static inline void assign (char_type & a, const char_type & b) noexcept
	{
		a = b;
	}
	static inline bool eq (char_type a, char_type b) noexcept
	{
		return a == b;
	}
	static inline bool lt (char_type a, char_type b) noexcept
	{
		return a < b;
	}
	static int compare (const char_type * a, const char_type * b, size_t size)
	{
		return std::char_traits<char>::compare (reinterpret_cast<char const *> (a), reinterpret_cast<char const *> (b), size);
	}
	static inline size_t length (const char_type * a)
	{
		return std::char_traits<char>::length (reinterpret_cast<char const *> (a));
	}
	static inline const char_type * find (const char_type * a, size_t size, const char_type & b)
	{
		return reinterpret_cast<const char_type *> (std::char_traits<char>::find (reinterpret_cast<char const *> (a), size, reinterpret_cast<char const &> (b)));
	}
	static char_type * move (char_type * a, const char_type * b, size_t size)
	{
		return reinterpret_cast<char_type *> (std::char_traits<char>::move (reinterpret_cast<char *> (a), reinterpret_cast<char const *> (b), size));
	}
	static char_type * copy (char_type * a, const char_type * b, size_t size)
	{
		return reinterpret_cast<char_type *> (std::char_traits<char>::copy (reinterpret_cast<char *> (a), reinterpret_cast<char const *> (b), size));
	}
	static char_type * assign (char_type * a, size_t size, char_type b)
	{
		return reinterpret_cast<char_type *> (std::char_traits<char>::assign (reinterpret_cast<char *> (a), size, reinterpret_cast<char const &> (b)));
	}
	static inline int_type not_eof (int_type v) noexcept
	{
		return std::char_traits<char>::not_eof (v);
	}
	static inline char_type to_char_type (int_type v) noexcept
	{
		return char_type (v);
	}
	static inline int_type to_int_type (char_type v) noexcept
	{
		return int_type (v);
	}
	static inline bool eq_int_type (int_type a, int_type b) noexcept
	{
		return a == b;
	}
	static inline int_type eof () noexcept
	{
		return std::char_traits<char>::eof ();
	}
};