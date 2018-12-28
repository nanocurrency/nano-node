#pragma once

#include <chrono>
#include <rai/lib/errors.hpp>
#include <rai/node/node.hpp>

namespace rai
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
	system (uint16_t, size_t);
	~system ();
	void generate_activity (rai::node &, std::vector<rai::account> &);
	void generate_mass_activity (uint32_t, rai::node &);
	void generate_usage_traffic (uint32_t, uint32_t, size_t);
	void generate_usage_traffic (uint32_t, uint32_t);
	rai::account get_random_account (std::vector<rai::account> &);
	rai::uint128_t get_random_amount (rai::transaction const &, rai::node &, rai::account const &);
	void generate_rollback (rai::node &, std::vector<rai::account> &);
	void generate_change_known (rai::node &, std::vector<rai::account> &);
	void generate_change_unknown (rai::node &, std::vector<rai::account> &);
	void generate_receive (rai::node &);
	void generate_send_new (rai::node &, std::vector<rai::account> &);
	void generate_send_existing (rai::node &, std::vector<rai::account> &);
	std::shared_ptr<rai::wallet> wallet (size_t);
	rai::account account (rai::transaction const &, size_t);
	/**
	 * Polls, sleep if there's no work to be done (default 50ms), then check the deadline
	 * @returns 0 or rai::deadline_expired
	 */
	std::error_code poll (const std::chrono::nanoseconds & sleep_time = std::chrono::milliseconds (50));
	void stop ();
	void deadline_set (const std::chrono::duration<double, std::nano> & delta);
	boost::asio::io_context io_ctx;
	rai::alarm alarm;
	std::vector<std::shared_ptr<rai::node>> nodes;
	rai::logging logging;
	rai::work_pool work;
	std::chrono::time_point<std::chrono::steady_clock, std::chrono::duration<double>> deadline{ std::chrono::steady_clock::time_point::max () };
	double deadline_scaling_factor{ 1.0 };
};
class landing_store
{
public:
	landing_store ();
	landing_store (rai::account const &, rai::account const &, uint64_t, uint64_t);
	landing_store (bool &, std::istream &);
	rai::account source;
	rai::account destination;
	uint64_t start;
	uint64_t last;
	bool deserialize (std::istream &);
	void serialize (std::ostream &) const;
	bool operator== (rai::landing_store const &) const;
};
class landing
{
public:
	landing (rai::node &, std::shared_ptr<rai::wallet>, rai::landing_store &, boost::filesystem::path const &);
	void write_store ();
	rai::uint128_t distribution_amount (uint64_t);
	void distribute_one ();
	void distribute_ongoing ();
	boost::filesystem::path path;
	rai::landing_store & store;
	std::shared_ptr<rai::wallet> wallet;
	rai::node & node;
	static int constexpr interval_exponent = 10;
	static std::chrono::seconds constexpr distribution_interval = std::chrono::seconds (1 << interval_exponent); // 1024 seconds
	static std::chrono::seconds constexpr sleep_seconds = std::chrono::seconds (7);
};
}
REGISTER_ERROR_CODES (rai, error_system);
