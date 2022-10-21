#pragma once

#include <nano/lib/utility.hpp>

#include <boost/endian/conversion.hpp>

#include <streambuf>

namespace nano
{
// We operate on streams of uint8_t by convention
using stream = std::basic_streambuf<uint8_t>;

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
 * Use little endian as standard message endianness, due to major platforms being little endian already (x86, arm)
 */
template <typename T>
void write_little_endian (nano::stream & stream, T const & value)
{
	nano::write (stream, boost::endian::native_to_little (value));
}

template <typename T>
void read_little_endian (nano::stream & stream, T & value)
{
	T tmp;
	nano::read (stream, tmp);
	value = boost::endian::little_to_native (tmp);
}
}
