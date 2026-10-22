#include <nano/lib/formatting.hpp>
#include <nano/node/election.hpp>
#include <nano/node/formatting.hpp>

#include <ostream>

#include <fmt/ranges.h>

namespace nano::log
{
std::ostream & operator<< (std::ostream & os, as_election_hashes_formatter const & wrapper)
{
	fmt::print (os, "{}", fmt::join (wrapper.election.blocks_hashes (), ", "));
	return os;
}
}
