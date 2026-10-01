#pragma once

#include <nano/lib/enum_flags.hpp>

#include <cstdint>
#include <string_view>

namespace nano
{
enum class node_capabilities : uint64_t
{
	none = 0,
	topo_index = 1ULL << 0,
	vote_storage = 1ULL << 1,
	no_ledger = 1ULL << 2, // Set when this node does not maintain a ledger
	vote_relay = 1ULL << 3, // Serves vote relay requests from its vote cache, querying reps on misses
};

std::string_view to_string (node_capabilities);

using node_capabilities_flags = nano::enum_flags<node_capabilities>;
}
