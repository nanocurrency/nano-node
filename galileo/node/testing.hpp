#pragma once

#include <chrono>
#include <galileo/lib/errors.hpp>
#include <galileo/node/node.hpp>

namespace galileo
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
	void generate_activity (galileo::node &, std::vector<galileo::account> &);
	void generate_mass_activity (uint32_t, galileo::node &);
	void generate_usage_traffic (uint32_t, uint32_t, size_t);
	void generate_usage_traffic (uint32_t, uint32_t);
	galileo::account get_random_account (std::vector<galileo::account> &);
	galileo::uint128_t get_random_amount (galileo::transaction const &, galileo::node &, galileo::account const &);
	void generate_rollback (galileo::node &, std::vector<galileo::account> &);
	void generate_change_known (galileo::node &, std::vector<galileo::account> &);
	void generate_change_unknown (galileo::node &, std::vector<galileo::account> &);
	void generate_receive (galileo::node &);
	void generate_send_new (galileo::node &, std::vector<galileo::account> &);
	void generate_send_existing (galileo::node &, std::vector<galileo::account> &);
	std::shared_ptr<galileo::wallet> wallet (size_t);
	galileo::account account (galileo::transaction const &, size_t);
	/**
	 * Polls, sleep if there's no work to be done (default 50ms), then check the deadline
	 * @returns 0 or galileo::deadline_expired
	 */
	std::error_code poll (const std::chrono::nanoseconds & sleep_time = std::chrono::milliseconds (50));
	void stop ();
	void deadline_set (const std::chrono::duration<double, std::nano> & delta);
	boost::asio::io_service service;
	galileo::alarm alarm;
	std::vector<std::shared_ptr<galileo::node>> nodes;
	galileo::logging logging;
	galileo::work_pool work;
	std::chrono::time_point<std::chrono::steady_clock, std::chrono::duration<double>> deadline{ std::chrono::steady_clock::time_point::max () };
	double deadline_scaling_factor{ 1.0 };
};
class landing_store
{
public:
	landing_store ();
	landing_store (galileo::account const &, galileo::account const &, uint64_t, uint64_t);
	landing_store (bool &, std::istream &);
	galileo::account source;
	galileo::account destination;
	uint64_t start;
	uint64_t last;
	bool deserialize (std::istream &);
	void serialize (std::ostream &) const;
	bool operator== (galileo::landing_store const &) const;
};
class landing
{
public:
	landing (galileo::node &, std::shared_ptr<galileo::wallet>, galileo::landing_store &, boost::filesystem::path const &);
	void write_store ();
	galileo::uint128_t distribution_amount (uint64_t);
	void distribute_one ();
	void distribute_ongoing ();
	boost::filesystem::path path;
	galileo::landing_store & store;
	std::shared_ptr<galileo::wallet> wallet;
	galileo::node & node;
	static int constexpr interval_exponent = 10;
	static std::chrono::seconds constexpr distribution_interval = std::chrono::seconds (1 << interval_exponent); // 1024 seconds
	static std::chrono::seconds constexpr sleep_seconds = std::chrono::seconds (7);
};
}
REGISTER_ERROR_CODES (galileo, error_system);
