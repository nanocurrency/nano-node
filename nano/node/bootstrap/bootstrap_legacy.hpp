#pragma once

#include <nano/node/bootstrap/bootstrap_attempt.hpp>

#include <boost/property_tree/ptree_fwd.hpp>

#include <atomic>
#include <deque>
#include <memory>
#include <vector>

namespace nano
{
class node;

/**
 * Legacy bootstrap session. This is made up of 3 phases: frontier requests, bootstrap pulls, bootstrap pushes.
 */
class bootstrap_attempt_legacy : public bootstrap_attempt
{
public:
	explicit bootstrap_attempt_legacy (std::shared_ptr<nano::node> const & node_a, uint64_t const incremental_id_a, std::string const & id_a, uint32_t const frontiers_age_a, nano::account const & start_account_a);
	void run () override;
	bool consume_future (std::future<bool> &);
	void stop () override;
	bool request_frontier (nano::unique_lock<nano::mutex> &, bool = false);
	void request_push (nano::unique_lock<nano::mutex> &);
	void add_frontier (nano::pull_info const &);
	void add_bulk_push_target (nano::block_hash const &, nano::block_hash const &) override;
	bool request_bulk_push_target (std::pair<nano::block_hash, nano::block_hash> &) override;
	void set_start_account (nano::account const &) override;
	void run_start (nano::unique_lock<nano::mutex> &);
	void get_information (boost::property_tree::ptree &) override;
	nano::tcp_endpoint endpoint_frontier_request;
	std::weak_ptr<nano::frontier_req_client> frontiers;
	std::weak_ptr<nano::bulk_push_client> push;
	std::deque<nano::pull_info> frontier_pulls;
	std::vector<std::pair<nano::block_hash, nano::block_hash>> bulk_push_targets;
	nano::account start_account{};
	std::atomic<unsigned> account_count{ 0 };
	uint32_t frontiers_age;
};
}
