#pragma once

namespace celerix
{
class node;
class telemetry_data;
}

namespace celerix::test
{
/**
 * Compares telemetry data without signatures
 * @return true if comparison OK
 */
bool compare_telemetry_data (celerix::telemetry_data const &, celerix::telemetry_data const &);

/**
 * Compares telemetry data and checks signature matches node_id
 * @return true if comparison OK
 */
bool compare_telemetry (celerix::telemetry_data const &, celerix::node const &);
}