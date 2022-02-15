#include <nano/lib/numbers.hpp>

std::string convert_raw_to_dec (std::string amount_raw, nano::uint128_t ratio = nano::BAN_ratio); // using 10^29 by default if not specifically set to other ratio