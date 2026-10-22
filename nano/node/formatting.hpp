#pragma once

#include <nano/node/fwd.hpp>

#include <iosfwd>

#include <fmt/ostream.h>

// Lazy formatting wrappers for node types
namespace nano::log
{
// An election's held block hashes, fetched only when the log line is emitted
struct as_election_hashes_formatter
{
	nano::election const & election;

	friend std::ostream & operator<< (std::ostream & os, as_election_hashes_formatter const & wrapper);
};

inline auto as_election_hashes (nano::election const & election)
{
	return as_election_hashes_formatter{ election };
}
}

template <>
struct fmt::formatter<nano::log::as_election_hashes_formatter> : fmt::ostream_formatter
{
};
