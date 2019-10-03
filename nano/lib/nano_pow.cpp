#include <nano/lib/nano_pow.hpp>

nano::nano_pow::nano_pow (nano::legacy_pow pow)
{
	bytes.fill (0);
	for (auto i (bytes.rbegin ()), n (bytes.rend ()); i != n; ++i)
	{
		*i = static_cast<uint8_t> (pow & static_cast<uint8_t> (0xff));
		pow >>= 8;
	}
}

nano::nano_pow::nano_pow (nano::nano_pow::real_byte_type const & bytes_a)
{
	// This will be big endian already, so just copy it to the rhs of the buffer
	bytes.fill (0);
	std::memcpy (bytes.data () + sizeof (bytes) - size, &bytes_a, size);
}

nano::nano_pow::nano_pow (num_type pow)
{
	bytes.fill (0);
	boost::multiprecision::export_bits (pow, bytes.rbegin (), 8, false);
}

nano::nano_pow::num_type nano::nano_pow::number () const
{
	num_type result;
	boost::multiprecision::import_bits (result, bytes.begin (), bytes.end ());
	return result;
}

nano::legacy_pow nano::nano_pow::as_legacy () const
{
	using legacy_pow_mp = boost::multiprecision::number<boost::multiprecision::cpp_int_backend<sizeof (nano::legacy_pow) * 8, sizeof (nano::legacy_pow) * 8, boost::multiprecision::unsigned_magnitude, boost::multiprecision::unchecked, void>>;
	legacy_pow_mp result;
	boost::multiprecision::import_bits (result, bytes.begin (), bytes.end ());
	return result.convert_to<nano::legacy_pow> ();
}

bool nano::nano_pow::operator== (nano::nano_pow const & other_a) const
{
	return bytes == other_a.bytes;
}

std::istream & operator>> (std::istream & input, nano::nano_pow & pow)
{
	nano::nano_pow::num_type num;
	input >> num;
	pow = num;
	return input;
}

std::ostream & operator<< (std::ostream & output, nano::nano_pow const & num)
{
	output << num.number ();
	return output;
}

bool nano::from_string_hex (std::string const & value_a, nano::nano_pow & target_a)
{
	nano::nano_pow::num_type target;
	auto error = from_string_hex (value_a, target);
	target_a = target;
	return error;
}

std::string nano::to_string_hex (nano::nano_pow const & value_a)
{
	std::stringstream stream;
	stream << std::hex << std::noshowbase << std::setw (nano::nano_pow::size * 2) << std::setfill ('0') << value_a;
	return stream.str ();
}

bool nano::try_read (nano::stream & stream_a, nano::nano_pow & value_a)
{
	nano::nano_pow::real_byte_type bytes;
	auto error = nano::try_read (stream_a, bytes);
	value_a = bytes;
	return error;
}

void nano::write (nano::stream & stream_a, nano::nano_pow const & value_a)
{
	nano::nano_pow::real_byte_type bytes;
	std::copy (value_a.bytes.cbegin () + value_a.bytes.size () - nano::nano_pow::size, value_a.bytes.cend (), bytes.begin ());
	write (stream_a, bytes);
}
