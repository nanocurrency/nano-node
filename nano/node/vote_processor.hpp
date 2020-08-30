#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/utility.hpp>
#include <nano/secure/common.hpp>

#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>

namespace nano
{
class signature_checker;
class active_transactions;
class block_store;
class node_observers;
class stats;
class node_config;
class logger_mt;
class online_reps;
class ledger;
class network_params;
class node_flags;

class transaction;
namespace transport
{
	class channel;
}

class vote_processor final
{
public:
	explicit vote_processor (nano::node & node_a);
	/** Returns false if the vote was processed */
	bool vote (std::shared_ptr<nano::vote>, std::shared_ptr<nano::transport::channel>);
	/** Note: node.active.mutex lock is required */
	nano::vote_code vote_blocking (std::shared_ptr<nano::vote>, std::shared_ptr<nano::transport::channel>, bool = false);
	void verify_votes (std::deque<std::pair<std::shared_ptr<nano::vote>, std::shared_ptr<nano::transport::channel>>> const &);
	void flush ();
	size_t size ();
	bool empty ();
	void calculate_weights ();
	void stop ();

private:
	void process_loop ();

	nano::node & node;
	std::deque<std::pair<std::shared_ptr<nano::vote>, std::shared_ptr<nano::transport::channel>>> votes;
	/** Representatives levels for random early detection */
	std::unordered_set<nano::account> representatives_1;
	std::unordered_set<nano::account> representatives_2;
	std::unordered_set<nano::account> representatives_3;
	nano::condition_variable condition;
	std::mutex mutex;
	bool started;
	bool stopped;
	bool is_active;
	std::thread thread;

	friend std::unique_ptr<container_info_component> collect_container_info (vote_processor & vote_processor, const std::string & name);
	friend class vote_processor_weights_Test;
};

std::unique_ptr<container_info_component> collect_container_info (vote_processor & vote_processor, const std::string & name);
}
