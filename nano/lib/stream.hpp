#pragma once

#include <nano/lib/utility.hpp>

#include <boost/endian/conversion.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/stream_buffer.hpp>

#include <streambuf>
#include <string>
#include <vector>

namespace nano
{
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

// We operate on streams of uint8_t by convention
using stream = std::basic_streambuf<uint8_t, uint8_char_traits>;
using bufferstream = boost::iostreams::stream_buffer<boost::iostreams::basic_array_source<uint8_t>, uint8_char_traits>;
using vectorstream = boost::iostreams::stream_buffer<boost::iostreams::back_insert_device<std::vector<uint8_t>>, uint8_char_traits>;

// Read a raw byte stream the size of `T' and fill value. Returns true if there was an error, false otherwise
template <typename T>
bool try_read (nano::stream & stream_a, T & value_a)
{
	static_assert (std::is_standard_layout<T>::value, "Can't stream read non-standard layout types");
	auto amount_read (stream_a.sgetn (reinterpret_cast<uint8_t *> (&value_a), sizeof (value_a)));
	return amount_read != sizeof (value_a);
}

// A wrapper of try_read which throws if there is an error
template <typename T>
void read (nano::stream & stream_a, T & value)
{
	auto error = try_read (stream_a, value);
	if (error)
	{
		throw std::runtime_error ("Failed to read type");
	}
}

inline void read (nano::stream & stream_a, std::vector<uint8_t> & value_a, size_t size_a)
{
	value_a.resize (size_a);
	if (stream_a.sgetn (value_a.data (), size_a) != size_a)
	{
		throw std::runtime_error ("Failed to read this number of bytes");
	}
}

template <typename T>
void write (nano::stream & stream_a, T const & value_a)
{
	static_assert (std::is_standard_layout<T>::value, "Can't stream write non-standard layout types");
	auto amount_written (stream_a.sputn (reinterpret_cast<uint8_t const *> (&value_a), sizeof (value_a)));
	(void)amount_written;
	debug_assert (amount_written == sizeof (value_a));
}

inline void write (nano::stream & stream_a, std::vector<uint8_t> const & value_a)
{
	auto amount_written (stream_a.sputn (value_a.data (), value_a.size ()));
	(void)amount_written;
	debug_assert (amount_written == value_a.size ());
}

inline bool at_end (nano::stream & stream)
{
	uint8_t junk;
	auto end (nano::try_read (stream, junk));
	return end;
}

/*
 * We use big endian as standard for all network communications
 */
template <typename T>
void write_big_endian (nano::stream & stream, T const & value)
{
	nano::write (stream, boost::endian::native_to_big (value));
}

template <typename T>
void read_big_endian (nano::stream & stream, T & value)
{
	T tmp;
	nano::read (stream, tmp);
	value = boost::endian::big_to_native (tmp);
}
}
