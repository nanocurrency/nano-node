#pragma once

namespace nano
{
class node;
class telemetry_data;
}

namespace nano::test
{
/**
 * Compares telemetry data and checks signature matches node_id
 * @return true if comparison OK
 */
bool compare_telemetry (nano::telemetry_data, nano::node const &);
}