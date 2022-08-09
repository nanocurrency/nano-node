#pragma once

#include <cstdint>

namespace nano
{
class keypair;
class network_params;
class telemetry_data;

namespace test
{
	void compare_default_telemetry_response_data_excluding_signature (nano::telemetry_data const & telemetry_data_a, nano::network_params const & network_params_a, uint64_t bandwidth_limit_a, uint64_t active_difficulty_a);
	void compare_default_telemetry_response_data (nano::telemetry_data const & telemetry_data_a, nano::network_params const & network_params_a, uint64_t bandwidth_limit_a, uint64_t active_difficulty_a, nano::keypair const & node_id_a);
}
}