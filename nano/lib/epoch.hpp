#pragma once

#include <cstdint>
#include <iostream>

namespace nano
{
/**
 * Tag for which epoch an entry belongs to
 */
enum class epoch : uint8_t
{
	invalid = 0,
	unspecified = 1,
	epoch_begin = 2,
	epoch_0 = 2,
	epoch_1 = 3,
	epoch_2 = 4,
	max = epoch_2 /* Update this as new epochs are added */
};

/* This turns epoch_0 into 0 for instance */
std::underlying_type_t<nano::epoch> normalized_epoch (nano::epoch epoch_a);
std::string epoch_as_string (nano::epoch epoch);
nano::epoch epoch_from_string (std::string const & str);
bool is_epoch_nano_pow (nano::epoch);
}

std::ostream & operator<< (std::ostream & os, nano::epoch const & epoch);
std::istream & operator>> (std::istream & istream, nano::epoch & epoch_a);
