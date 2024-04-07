#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/processing_queue.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/wallet.hpp>
#include <nano/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <condition_variable>
#include <deque>
#include <thread>

namespace mi = boost::multi_index;

namespace nano
{
class ledger;
class local_vote_history;
class network;
class node;
class node_config;
class stats;
class vote_processor;
class vote_spacing;
class wallets;
}
namespace nano::secure
{
class transaction;
class write_transaction;
}
namespace nano::transport
{
class channel;
}

namespace nano
{
class vote_generator final
{
private:
	using candidate_t = std::pair<nano::root, nano::block_hash>;
	using request_t = std::pair<std::vector<candidate_t>, std::shared_ptr<nano::transport::channel>>;
	using queue_entry_t = std::pair<nano::root, nano::block_hash>;

public:
	vote_generator (nano::node_config const &, nano::node &, nano::ledger &, nano::wallets &, nano::vote_processor &, nano::local_vote_history &, nano::network &, nano::stats &, nano::logger &, bool is_final);
	~vote_generator ();

	/** Queue items for vote generation, or broadcast votes already in cache */
	void add (nano::root const &, nano::block_hash const &);
	/** Queue blocks for vote generation, returning the number of successful candidates.*/
	std::size_t generate (std::vector<std::shared_ptr<nano::block>> const & blocks_a, std::shared_ptr<nano::transport::channel> const & channel_a);
	void set_reply_action (std::function<void (std::shared_ptr<nano::vote> const &, std::shared_ptr<nano::transport::channel> const &)>);

	void start ();
	void stop ();

	std::unique_ptr<container_info_component> collect_container_info (std::string const & name) const;

private:
	void run ();
	void broadcast (nano::unique_lock<nano::mutex> &);
	void reply (nano::unique_lock<nano::mutex> &, request_t &&);
	void vote (std::vector<nano::block_hash> const &, std::vector<nano::root> const &, std::function<void (std::shared_ptr<nano::vote> const &)> const &);
	void broadcast_action (std::shared_ptr<nano::vote> const &) const;
	void process_batch (std::deque<queue_entry_t> & batch);
	/**
	 * Check if block is eligible for vote generation
	 * @param transaction : needs `tables::final_votes` lock
	 * @return: Should vote
	 */
	bool should_vote (secure::write_transaction const &, nano::root const &, nano::block_hash const &);

private:
	std::function<void (std::shared_ptr<nano::vote> const &, std::shared_ptr<nano::transport::channel> &)> reply_action; // must be set only during initialization by using set_reply_action

private: // Dependencies
	nano::node_config const & config;
	nano::node & node;
	nano::ledger & ledger;
	nano::wallets & wallets;
	nano::vote_processor & vote_processor;
	nano::local_vote_history & history;
	std::unique_ptr<nano::vote_spacing> spacing_impl;
	nano::vote_spacing & spacing;
	nano::network & network;
	nano::stats & stats;
	nano::logger & logger;

private:
	processing_queue<queue_entry_t> vote_generation_queue;

private:
	const bool is_final;
	mutable nano::mutex mutex;
	nano::condition_variable condition;
	static std::size_t constexpr max_requests{ 2048 };
	std::deque<request_t> requests;
	std::deque<candidate_t> candidates;
	std::atomic<bool> stopped{ false };
	std::thread thread;
};
}
