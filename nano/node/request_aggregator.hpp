#pragma once

#include <celerix/lib/locks.hpp>
#include <celerix/lib/numbers.hpp>
#include <celerix/lib/threading.hpp>
#include <celerix/node/fair_queue.hpp>
#include <celerix/node/fwd.hpp>
#include <celerix/node/transport/channel.hpp>
#include <celerix/node/transport/transport.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include <condition_variable>
#include <thread>
#include <unordered_map>
#include <vector>

namespace mi = boost::multi_index;

namespace celerix
{
class request_aggregator_config final
{
public:
	celerix::error deserialize (celerix::tomlconfig &);
	celerix::error serialize (celerix::tomlconfig &) const;

public:
	size_t threads{ std::clamp (celerix::hardware_concurrency () / 2, 1u, 4u) };
	size_t max_queue{ 128 };
	size_t batch_size{ 16 };
};

/**
 * Pools together confirmation requests, separately for each endpoint.
 * Requests are added from network messages, and aggregated to minimize bandwidth and vote generation. Example:
 * * Two votes are cached, one for hashes {1,2,3} and another for hashes {4,5,6}
 * * A request arrives for hashes {1,4,5}. Another request arrives soon afterwards for hashes {2,3,6}
 * * The aggregator will reply with the two cached votes
 * Votes are generated for uncached hashes.
 */
class request_aggregator final
{
public:
	request_aggregator (request_aggregator_config const &, celerix::node &, celerix::stats &, celerix::vote_generator &, celerix::vote_generator &, celerix::local_vote_history &, celerix::ledger &, celerix::wallets &, celerix::vote_router &);
	~request_aggregator ();

	void start ();
	void stop ();

	using request_type = std::vector<std::pair<celerix::block_hash, celerix::root>>;

	/** Add a new request by \p channel_a for hashes \p hashes_roots_a */
	bool request (request_type const & request, std::shared_ptr<celerix::transport::channel> const &);

	/** Returns the number of currently queued request pools */
	std::size_t size () const;
	bool empty () const;

	celerix::container_info container_info () const;

private:
	void run ();
	void run_batch (celerix::unique_lock<celerix::mutex> & lock);
	void process (celerix::secure::transaction const &, request_type const &, std::shared_ptr<celerix::transport::channel> const &);

	/** Remove duplicate requests **/
	void erase_duplicates (std::vector<std::pair<celerix::block_hash, celerix::root>> &) const;

	struct aggregate_result
	{
		std::vector<std::shared_ptr<celerix::block>> remaining_normal;
		std::vector<std::shared_ptr<celerix::block>> remaining_final;
	};

	/** Aggregate \p requests_a and send cached votes to \p channel_a . Return the remaining hashes that need vote generation for each block for regular & final vote generators **/
	aggregate_result aggregate (celerix::secure::transaction const &, request_type const &, std::shared_ptr<celerix::transport::channel> const &) const;

	void reply_action (std::shared_ptr<celerix::vote> const & vote_a, std::shared_ptr<celerix::transport::channel> const & channel_a) const;

private: // Dependencies
	request_aggregator_config const & config;
	celerix::network_constants const & network_constants;
	celerix::stats & stats;
	celerix::local_vote_history & local_votes;
	celerix::ledger & ledger;
	celerix::wallets & wallets;
	celerix::vote_router & vote_router;
	celerix::vote_generator & generator;
	celerix::vote_generator & final_generator;

private:
	using value_type = std::pair<request_type, std::shared_ptr<celerix::transport::channel>>;
	celerix::fair_queue<value_type, celerix::no_value> queue;

	bool stopped{ false };
	celerix::condition_variable condition;
	mutable celerix::mutex mutex{ mutex_identifier (mutexes::request_aggregator) };
	std::vector<std::thread> threads;
};
}
