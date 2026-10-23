#pragma once

#include <nano/lib/enum_flags.hpp>

#include <cstdint>
#include <string_view>

namespace nano
{
enum class node_capabilities : uint64_t
{
	none = 0, // No capabilities advertised
	topo_index = 1ULL << 0, // Serves topological bootstrap requests
	// 1ULL << 1 is reserved
	no_ledger = 1ULL << 2, // Does not maintain a ledger
	vote_relay = 1ULL << 3, // Serves vote relay requests
};

std::string_view to_string (node_capabilities);

using node_capabilities_flags = nano::enum_flags<node_capabilities>;
}
