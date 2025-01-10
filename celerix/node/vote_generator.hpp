#pragma once

#include <celerix/lib/locks.hpp>
#include <celerix/lib/logging.hpp>
#include <celerix/lib/numbers.hpp>
#include <celerix/lib/processing_queue.hpp>
#include <celerix/lib/utility.hpp>
#include <celerix/node/fwd.hpp>
#include <celerix/node/wallet.hpp>
#include <celerix/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <condition_variable>
#include <deque>
#include <thread>
#include <variant>

namespace mi = boost::multi_index;

namespace celerix
{
class vote_generator final
{
private:
	using candidate_t = std::pair<celerix::root, celerix::block_hash>;
	using request_t = std::pair<std::vector<candidate_t>, std::shared_ptr<celerix::transport::channel>>;
	using queue_entry_t = std::pair<celerix::root, celerix::block_hash>;
	std::chrono::steady_clock::time_point next_broadcast = { std::chrono::steady_clock::now () };

public:
	vote_generator (celerix::node_config const &, celerix::node &, celerix::ledger &, celerix::wallets &, celerix::vote_processor &, celerix::local_vote_history &, celerix::network &, celerix::stats &, celerix::logger &, bool is_final);
	~vote_generator ();

	/** Queue items for vote generation, or broadcast votes already in cache */
	void add (celerix::root const &, celerix::block_hash const &);
	/** Queue blocks for vote generation, returning the number of successful candidates.*/
	std::size_t generate (std::vector<std::shared_ptr<celerix::block>> const & blocks_a, std::shared_ptr<celerix::transport::channel> const & channel_a);

	void start ();
	void stop ();

	celerix::container_info container_info () const;

private:
	using transaction_variant_t = std::variant<celerix::secure::read_transaction, celerix::secure::write_transaction>;

	void run ();
	void broadcast (celerix::unique_lock<celerix::mutex> &);
	void reply (celerix::unique_lock<celerix::mutex> &, request_t &&);
	void vote (std::vector<celerix::block_hash> const &, std::vector<celerix::root> const &, std::function<void (std::shared_ptr<celerix::vote> const &)> const &);
	void broadcast_action (std::shared_ptr<celerix::vote> const &) const;
	void process_batch (std::deque<queue_entry_t> & batch);
	bool should_vote (transaction_variant_t const &, celerix::root const &, celerix::block_hash const &) const;
	bool broadcast_predicate () const;

private: // Dependencies
	celerix::node_config const & config;
	celerix::node & node;
	celerix::ledger & ledger;
	celerix::wallets & wallets;
	celerix::vote_processor & vote_processor;
	celerix::local_vote_history & history;
	std::unique_ptr<celerix::vote_spacing> spacing_impl;
	celerix::vote_spacing & spacing;
	celerix::network & network;
	celerix::stats & stats;
	celerix::logger & logger;

private:
	celerix::processing_queue<queue_entry_t> vote_generation_queue;

private:
	const bool is_final;
	mutable celerix::mutex mutex;
	celerix::condition_variable condition;
	static std::size_t constexpr max_requests{ 2048 };
	std::deque<request_t> requests;
	std::deque<candidate_t> candidates;
	std::atomic<bool> stopped{ false };
	std::thread thread;
	std::shared_ptr<celerix::transport::channel> inproc_channel;
};
}
