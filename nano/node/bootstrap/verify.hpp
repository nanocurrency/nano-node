#pragma once

#include <nano/messages/asc_pull.hpp>
#include <nano/node/bootstrap/common.hpp>
#include <nano/node/bootstrap/queries.hpp>

namespace nano::bootstrap
{
// Validates that a blocks response forms a continuous chain matching the queried start
verify_result verify (nano::messages::asc_pull_ack::blocks_payload const &, blocks_query const &);

// Validates a frontiers response against its query: ascending account order, nothing below the start
verify_result verify (nano::messages::asc_pull_ack::frontiers_payload const &, frontiers_query const &);

// Validates a topo index response: strictly ascending topo keys with no skipped heights, nothing below the start, within count
verify_result verify (nano::messages::asc_pull_ack::topo_index_payload const &, topo_index_query const &);

// Validates a random blocks response: every returned block was requested, no duplicates
verify_result verify (nano::messages::asc_pull_ack::blocks_payload const &, blocks_random_query const &);
}
