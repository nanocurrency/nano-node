#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/threading.hpp>
#include <nano/node/fair_queue.hpp>
#include <nano/node/fwd.hpp>
#include <nano/node/transport/channel.hpp>
#include <nano/node/transport/transport.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include <condition_variable>
#include <thread>
#include <unordered_map>
#include <vector>

namespace mi = boost::multi_index;

namespace nano
{
class request_aggregator_config final
{
public:
	nano::error deserialize (nano::tomlconfig &);
	nano::error serialize (nano::tomlconfig &) const;

public:
	size_t threads{ std::clamp (nano::hardware_concurrency () / 2, 1u, 4u) };
	size_t max_queue{ 512 };
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
	request_aggregator (request_aggregator_config const &, nano::node &, nano::stats &, nano::vote_generator &, nano::vote_generator &, nano::local_vote_history &, nano::ledger &, nano::wallets &, nano::vote_router &);
	~request_aggregator ();

	void start ();
	void stop ();

public:
	using request_type = std::vector<std::pair<nano::block_hash, nano::root>>;

	/** Add a new request by \p channel_a for hashes \p hashes_roots_a */
	bool request (request_type const & request, std::shared_ptr<nano::transport::channel> const &);

	/** Returns the number of currently queued request pools */
	std::size_t size () const;
	bool empty () const;

	std::unique_ptr<container_info_component> collect_container_info (std::string const & name) const;

private:
	void run ();
	void run_batch (nano::unique_lock<nano::mutex> & lock);
	void process (nano::secure::transaction const &, request_type const &, std::shared_ptr<nano::transport::channel> const &);

	/** Remove duplicate requests **/
	void erase_duplicates (std::vector<std::pair<nano::block_hash, nano::root>> &) const;

	struct aggregate_result
	{
		std::vector<std::shared_ptr<nano::block>> remaining_normal;
		std::vector<std::shared_ptr<nano::block>> remaining_final;
	};

	/** Aggregate \p requests_a and send cached votes to \p channel_a . Return the remaining hashes that need vote generation for each block for regular & final vote generators **/
	aggregate_result aggregate (nano::secure::transaction const &, request_type const &, std::shared_ptr<nano::transport::channel> const &) const;

	void reply_action (std::shared_ptr<nano::vote> const & vote_a, std::shared_ptr<nano::transport::channel> const & channel_a) const;

private: // Dependencies
	request_aggregator_config const & config;
	nano::network_constants const & network_constants;
	nano::stats & stats;
	nano::local_vote_history & local_votes;
	nano::ledger & ledger;
	nano::wallets & wallets;
	nano::vote_router & vote_router;
	nano::vote_generator & generator;
	nano::vote_generator & final_generator;

private:
	using value_type = std::pair<request_type, std::shared_ptr<nano::transport::channel>>;
	nano::fair_queue<value_type, nano::no_value> queue;

	bool stopped{ false };
	nano::condition_variable condition;
	mutable nano::mutex mutex{ mutex_identifier (mutexes::request_aggregator) };
	std::vector<std::thread> threads;
};
}
