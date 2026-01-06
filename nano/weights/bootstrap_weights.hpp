#pragma once

#include <nano/lib/fwd.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/numbers_templ.hpp>

#include <unordered_map>

/*
 * Bootstrap weights provide a hardcoded set of representative weights used during initial node synchronization.
 * These pre-configured weights allow the node to make informed decisions about which blocks to trust
 * until it has synchronized enough data to calculate actual representative weights from the ledger.
 */
namespace nano
{
struct bootstrap_weights
{
	uint64_t max_blocks{ 0 };
	std::unordered_map<nano::account, nano::uint128_t> representatives;
};

bootstrap_weights get_bootstrap_weights (nano::network_type);
}
