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
};

std::string_view to_string (node_capabilities);

using node_capabilities_flags = nano::enum_flags<node_capabilities>;
}
