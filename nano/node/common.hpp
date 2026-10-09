#pragma once

#include <nano/lib/numbers.hpp>

// Vocabulary types shared between node components
namespace nano
{
/** Representative weight by how this node can reach it, each figure a sum of ledger weights */
struct stake_totals final
{
	nano::uint128_t peered{ 0 }; // Representatives with a direct channel
	nano::uint128_t relayed{ 0 }; // Representatives whose votes a relay delivered recently
	nano::uint128_t reachable{ 0 }; // Peered or relayed, each representative counted once
	nano::uint128_t online{ 0 }; // Representatives seen voting recently by any path
};
}
