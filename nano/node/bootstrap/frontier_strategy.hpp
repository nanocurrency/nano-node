#pragma once

#include <nano/lib/thread_pool.hpp>
#include <nano/node/bootstrap/bootstrap_context.hpp>

#include <deque>
#include <memory>
#include <thread>
#include <utility>

namespace nano::bootstrap
{
class frontier_strategy
{
public:
	explicit frontier_strategy (bootstrap_context & ctx);

	void start ();
	void stop ();
	void run_one ();

	bool process (nano::messages::asc_pull_ack::frontiers_payload const & response, async_tag const & tag);

private:
	void run ();

	nano::account wait_frontier ();
	bool request_frontiers (nano::account start, std::shared_ptr<nano::transport::channel> const & channel);
	void process_frontiers (std::deque<std::pair<nano::account, nano::block_hash>> const & frontiers);

	bootstrap_context & ctx;
	std::thread thread;

	// Dedicated pool for the frontier processing tasks (ledger reads)
	nano::thread_pool workers;
};
}
