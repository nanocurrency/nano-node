#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/processing_queue.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/voting/spacing.hpp>
#include <nano/node/wallet.hpp>
#include <nano/secure/common.hpp>

#include <condition_variable>
#include <deque>
#include <thread>

namespace mi = boost::multi_index;

namespace nano::voting
{
class history;
class processor;
}
namespace nano
{
class ledger;
class network;
class node_config;
class stats;
class wallets;
namespace transport
{
	class channel;
}
} // namespace nano

namespace nano::voting
{
class generator final
{
private:
	using candidate_t = std::pair<nano::root, nano::block_hash>;
	using request_t = std::pair<std::vector<candidate_t>, std::shared_ptr<nano::transport::channel>>;
	using queue_entry_t = std::pair<nano::root, nano::block_hash>;

public:
	generator (nano::node_config const & config_a, nano::ledger & ledger_a, nano::wallets & wallets_a, nano::voting::processor & vote_processor_a, nano::voting::history & history_a, nano::network & network_a, nano::stats & stats_a, bool is_final_a);
	~generator ();

	/** Queue items for vote generation, or broadcast votes already in cache */
	void add (nano::root const &, nano::block_hash const &);
	/** Queue blocks for vote generation, returning the number of successful candidates.*/
	std::size_t generate (std::vector<std::shared_ptr<nano::block>> const & blocks_a, std::shared_ptr<nano::transport::channel> const & channel_a);
	void set_reply_action (std::function<void (std::shared_ptr<nano::vote> const &, std::shared_ptr<nano::transport::channel> const &)>);

	void start ();
	void stop ();

private:
	void run ();
	void broadcast (nano::unique_lock<nano::mutex> &);
	void reply (nano::unique_lock<nano::mutex> &, request_t &&);
	void vote (std::vector<nano::block_hash> const &, std::vector<nano::root> const &, std::function<void (std::shared_ptr<nano::vote> const &)> const &);
	void broadcast_action (std::shared_ptr<nano::vote> const &) const;
	void process_batch (std::deque<queue_entry_t> & batch);
	/**
	 * Check if block is eligible for vote generation, then generates a vote or broadcasts votes already in cache
	 * @param transaction : needs `tables::final_votes` lock
	 */
	void process (store::write_transaction const &, nano::root const &, nano::block_hash const &);

private:
	std::function<void (std::shared_ptr<nano::vote> const &, std::shared_ptr<nano::transport::channel> &)> reply_action; // must be set only during initialization by using set_reply_action

private: // Dependencies
	nano::node_config const & config;
	nano::ledger & ledger;
	nano::wallets & wallets;
	nano::voting::processor & vote_processor;
	nano::voting::history & history;
	nano::voting::spacing spacing;
	nano::network & network;
	nano::stats & stats;

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

	friend std::unique_ptr<container_info_component> collect_container_info (generator & vote_generator, std::string const & name);
};

std::unique_ptr<container_info_component> collect_container_info (generator & generator, std::string const & name);
} // namespace nano::voting
