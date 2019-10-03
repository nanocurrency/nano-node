#pragma once

namespace nano
{
// We operate on streams of uint8_t by convention
using stream = std::basic_streambuf<uint8_t>;
// Read a raw byte stream the size of `T' and fill value. Returns true if there was an error, false otherwise
template <typename T>
bool try_read (nano::stream & stream_a, T & value)
{
	static_assert (std::is_standard_layout<T>::value, "Can't stream read non-standard layout types");
	auto amount_read (stream_a.sgetn (reinterpret_cast<uint8_t *> (&value), sizeof (value)));
	return amount_read != sizeof (value);
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

template <typename T>
void write (nano::stream & stream_a, T const & value)
{
	static_assert (std::is_standard_layout<T>::value, "Can't stream write non-standard layout types");
	auto amount_written (stream_a.sputn (reinterpret_cast<uint8_t const *> (&value), sizeof (value)));
	(void)amount_written;
	assert (amount_written == sizeof (value));
}
}