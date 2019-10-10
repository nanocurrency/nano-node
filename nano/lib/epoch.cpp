#include <nano/lib/epoch.hpp>

#include <boost/lexical_cast/try_lexical_convert.hpp>

namespace
{
// Returns 0 is epochs are equal, -1 if lhs is less than rhs and 1 if rhs is greater than lhs
int compare (nano::epoch lhs_a, nano::epoch rhs_a)
{
	auto epoch_lhs = std::underlying_type_t<nano::epoch> (lhs_a);
	auto epoch_rhs = std::underlying_type_t<nano::epoch> (rhs_a);
	return (epoch_lhs < epoch_rhs) ? -1 : (epoch_lhs > epoch_rhs);
}

bool is_epoch_greater (nano::epoch lhs_a, nano::epoch rhs_a)
{
	return compare (lhs_a, rhs_a) > 0;
}
}

std::string nano::epoch_as_string (nano::epoch epoch)
{
	assert (epoch != nano::epoch::invalid && epoch != nano::epoch::unspecified);
	auto epoch_num = nano::normalized_epoch (epoch);
	return std::to_string (epoch_num);
}

nano::epoch nano::epoch_from_string (std::string const & str)
{
	unsigned result;
	if (boost::conversion::try_lexical_convert (str, result) && (result <= nano::normalized_epoch (nano::epoch::max)))
	{
		return static_cast<nano::epoch> (result + std::underlying_type_t<nano::epoch> (nano::epoch::epoch_0));
	}

	return nano::epoch::epoch_0;
}

std::underlying_type_t<nano::epoch> nano::normalized_epoch (nano::epoch epoch_a)
{
	// Currently assumes that the epoch versions in the enum are sequential.
	auto start = static_cast<std::underlying_type_t<nano::epoch>> (nano::epoch::epoch_0);
	auto end = static_cast<std::underlying_type_t<nano::epoch>> (epoch_a);
	assert (end >= start);
	return end - start;
}

bool nano::is_epoch_nano_pow (nano::epoch epoch_a)
{
	return is_epoch_greater (epoch_a, nano::epoch::epoch_1);
}

std::ostream & nano::operator<< (std::ostream & os, nano::epoch const & epoch)
{
	return os << epoch_as_string (epoch);
}

std::istream & nano::operator>> (std::istream & istream, nano::epoch & epoch_a)
{
	std::underlying_type_t<nano::epoch> epoch;
	istream >> epoch;

	epoch_a = (epoch <= static_cast<std::underlying_type_t<nano::epoch>> (nano::epoch::max)) ? static_cast<nano::epoch> (epoch) : nano::epoch::invalid;
	return istream;
}
