#pragma once

#include <chrono>
#include <nano/lib/errors.hpp>
#include <nano/node/node.hpp>

namespace nano
{
/** Test-system related error codes */
enum class error_system
{
	generic = 1,
	deadline_expired
};
class system
{
public:
	system (uint16_t, uint16_t);
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
	boost::asio::io_context io_ctx;
	nano::alarm alarm;
	std::vector<std::shared_ptr<nano::node>> nodes;
	nano::logging logging;
	nano::work_pool work;
	std::chrono::time_point<std::chrono::steady_clock, std::chrono::duration<double>> deadline{ std::chrono::steady_clock::time_point::max () };
	double deadline_scaling_factor{ 1.0 };
};
class landing_store
{
public:
	landing_store ();
	landing_store (nano::account const &, nano::account const &, uint64_t, uint64_t);
	landing_store (bool &, std::istream &);
	nano::account source;
	nano::account destination;
	uint64_t start;
	uint64_t last;
	void serialize (std::ostream &) const;
	bool deserialize (std::istream &);
	bool operator== (nano::landing_store const &) const;
};
class landing
{
public:
	landing (nano::node &, std::shared_ptr<nano::wallet>, nano::landing_store &, boost::filesystem::path const &);
	void write_store ();
	nano::uint128_t distribution_amount (uint64_t);
	void distribute_one ();
	void distribute_ongoing ();
	boost::filesystem::path path;
	nano::landing_store & store;
	std::shared_ptr<nano::wallet> wallet;
	nano::node & node;
	static int constexpr interval_exponent = 10;
	static std::chrono::seconds constexpr distribution_interval = std::chrono::seconds (1 << interval_exponent); // 1024 seconds
	static std::chrono::seconds constexpr sleep_seconds = std::chrono::seconds (7);
};
}
REGISTER_ERROR_CODES (nano, error_system);
