#pragma once

namespace nano
{
class node;
}

namespace nano::messages
{
class telemetry_data;
}

namespace nano::test
{
/**
 * Compares telemetry data without signatures
 * @return true if comparison OK
 */
bool compare_telemetry_data (nano::messages::telemetry_data const &, nano::messages::telemetry_data const &);

/**
 * Compares telemetry data and checks signature matches node_id
 * @return true if comparison OK
 */
bool compare_telemetry (nano::messages::telemetry_data const &, nano::node const &);
}