#pragma once

#include <nano/lib/errors.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/node.hpp>

#include <chrono>

namespace nano
{
/** Test-system related error codes */
enum class error_system
{
	generic = 1,
	deadline_expired
};
class system final
{
public:
	system ();
	system (uint16_t, nano::transport::transport_type = nano::transport::transport_type::tcp);
	~system ();
	void generate_activity (nano::node &, std::vector<nano::account> &);
	void generate_mass_activity (uint32_t, nano::node &);
	void generate_usage_traffic (uint32_t, uint32_t, size_t);
	void generate_usage_traffic (uint32_t, uint32_t);
	nano::account get_random_account (std::vector<nano::account> &);
	nano::uint128_t get_random_amount (nano::transaction const &, nano::node &, nano::account const &);
	void generate_rollback (nano::node &, std::vector<nano::account> &);
	void generate_change_known (nano::node &, std::vector<nano::account> &);
	void generate_change_unknown (nano::node &, std::vector<nano::account> &);
	void generate_receive (nano::node &);
	void generate_send_new (nano::node &, std::vector<nano::account> &);
	void generate_send_existing (nano::node &, std::vector<nano::account> &);
	std::shared_ptr<nano::wallet> wallet (size_t);
	nano::account account (nano::transaction const &, size_t);
	/**
	 * Polls, sleep if there's no work to be done (default 50ms), then check the deadline
	 * @returns 0 or nano::deadline_expired
	 */
	std::error_code poll (const std::chrono::nanoseconds & sleep_time = std::chrono::milliseconds (50));
	void stop ();
	void deadline_set (const std::chrono::duration<double, std::nano> & delta);
	std::shared_ptr<nano::node> add_node (nano::node_flags = nano::node_flags (), nano::transport::transport_type = nano::transport::transport_type::tcp);
	std::shared_ptr<nano::node> add_node (nano::node_config const &, nano::node_flags = nano::node_flags (), nano::transport::transport_type = nano::transport::transport_type::tcp);
	boost::asio::io_context io_ctx;
	nano::alarm alarm{ io_ctx };
	std::vector<std::shared_ptr<nano::node>> nodes;
	nano::logging logging;
	nano::work_pool work{ 1 };
	std::chrono::time_point<std::chrono::steady_clock, std::chrono::duration<double>> deadline{ std::chrono::steady_clock::time_point::max () };
	double deadline_scaling_factor{ 1.0 };
};
}
REGISTER_ERROR_CODES (nano, error_system);
