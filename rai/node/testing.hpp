#pragma once

#include <rai/node/node.hpp>

namespace rai
{
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
	rai::uint128_t get_random_amount (MDB_txn *, rai::node &, rai::account const &);
	void generate_rollback (rai::node &, std::vector<rai::account> &);
	void generate_change_known (rai::node &, std::vector<rai::account> &);
	void generate_change_unknown (rai::node &, std::vector<rai::account> &);
	void generate_receive (rai::node &);
	void generate_send_new (rai::node &, std::vector<rai::account> &);
	void generate_send_existing (rai::node &, std::vector<rai::account> &);
	std::shared_ptr<rai::wallet> wallet (size_t);
	rai::account account (MDB_txn *, size_t);
	void poll ();
	void stop ();
	boost::asio::io_service service;
	rai::alarm alarm;
	std::vector<std::shared_ptr<rai::node>> nodes;
	rai::logging logging;
	rai::work_pool work;
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
