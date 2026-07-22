#include <nano/lib/formatting.hpp>
#include <nano/lib/ratios.hpp>

namespace nano::log
{
std::ostream & operator<< (std::ostream & os, as_nano_formatter const & wrapper)
{
	nano::encode_balance (os, wrapper.value, nano::nano_ratio, wrapper.precision, true);
	return os;
}

std::ostream & operator<< (std::ostream & os, as_raw_nano_formatter const & wrapper)
{
	os << wrapper.value;
	return os;
}
}
