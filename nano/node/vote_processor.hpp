#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/utility.hpp>
#include <nano/secure/common.hpp>

#include <boost/thread/thread.hpp>

#include <deque>
#include <memory>
#include <mutex>
#include <unordered_set>

namespace nano
{
class node;
class transaction;
namespace transport
{
	class channel;
}

class vote_processor final
{
public:
	explicit vote_processor (nano::node &);
	void vote (std::shared_ptr<nano::vote>, std::shared_ptr<nano::transport::channel>);
	/** Note: node.active.mutex lock is required */
	nano::vote_code vote_blocking (nano::transaction const &, std::shared_ptr<nano::vote>, std::shared_ptr<nano::transport::channel>, bool = false);
	void verify_votes (std::deque<std::pair<std::shared_ptr<nano::vote>, std::shared_ptr<nano::transport::channel>>> &);
	void flush ();
	void calculate_weights ();
	nano::node & node;
	void stop ();

private:
	void process_loop ();
	std::deque<std::pair<std::shared_ptr<nano::vote>, std::shared_ptr<nano::transport::channel>>> votes;
	/** Representatives levels for random early detection */
	std::unordered_set<nano::account> representatives_1;
	std::unordered_set<nano::account> representatives_2;
	std::unordered_set<nano::account> representatives_3;
	std::condition_variable condition;
	std::mutex mutex;
	bool started;
	bool stopped;
	bool active;
	boost::thread thread;

	friend std::unique_ptr<seq_con_info_component> collect_seq_con_info (vote_processor & vote_processor, const std::string & name);
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (vote_processor & vote_processor, const std::string & name);
}
