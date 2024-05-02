#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
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
	/**
	 * Holds a buffer of incoming requests from an endpoint.
	 * Extends the lifetime of the corresponding channel. The channel is updated on a new request arriving from the same endpoint, such that only the newest channel is held
	 */
	struct channel_pool final
	{
		channel_pool () = delete;
		explicit channel_pool (std::shared_ptr<nano::transport::channel> const & channel_a) :
			channel (channel_a),
			endpoint (nano::transport::map_endpoint_to_v6 (channel_a->get_endpoint ()))
		{
		}
		std::vector<std::pair<nano::block_hash, nano::root>> hashes_roots;
		std::shared_ptr<nano::transport::channel> channel;
		nano::endpoint endpoint;
		std::chrono::steady_clock::time_point const start{ std::chrono::steady_clock::now () };
		std::chrono::steady_clock::time_point deadline;
	};

	// clang-format off
	class tag_endpoint {};
	class tag_deadline {};
	// clang-format on

public:
	request_aggregator (nano::node_config const & config, nano::stats & stats_a, nano::vote_generator &, nano::vote_generator &, nano::local_vote_history &, nano::ledger &, nano::wallets &, nano::active_transactions &);
	~request_aggregator ();

	void start ();
	void stop ();

	/** Add a new request by \p channel_a for hashes \p hashes_roots_a */
	void add (std::shared_ptr<nano::transport::channel> const &, std::vector<std::pair<nano::block_hash, nano::root>> const & hashes_roots);

	/** Returns the number of currently queued request pools */
	std::size_t size () const;
	bool empty () const;

	std::chrono::milliseconds const max_delay;
	std::chrono::milliseconds const small_delay;
	std::size_t const max_channel_requests;
	std::size_t const request_aggregator_threads;

private:
	void run ();
	/** Remove duplicate requests **/
	void erase_duplicates (std::vector<std::pair<nano::block_hash, nano::root>> &) const;

	struct aggregate_result
	{
		std::vector<std::shared_ptr<nano::block>> remaining_normal;
		std::vector<std::shared_ptr<nano::block>> remaining_final;
	};

	/** Aggregate \p requests_a and send cached votes to \p channel_a . Return the remaining hashes that need vote generation for each block for regular & final vote generators **/
	aggregate_result aggregate (std::vector<std::pair<nano::block_hash, nano::root>> const & requests, std::shared_ptr<nano::transport::channel> const &) const;

	void reply_action (std::shared_ptr<nano::vote> const & vote_a, std::shared_ptr<nano::transport::channel> const & channel_a) const;

private: // Dependencies
	nano::node_config const & config;
	nano::stats & stats;
	nano::local_vote_history & local_votes;
	nano::ledger & ledger;
	nano::wallets & wallets;
	nano::active_transactions & active;
	nano::vote_generator & generator;
	nano::vote_generator & final_generator;

private:
	// clang-format off
	boost::multi_index_container<channel_pool,
	mi::indexed_by<
		mi::hashed_unique<mi::tag<tag_endpoint>,
			mi::member<channel_pool, nano::endpoint, &channel_pool::endpoint>>,
		mi::ordered_non_unique<mi::tag<tag_deadline>,
			mi::member<channel_pool, std::chrono::steady_clock::time_point, &channel_pool::deadline>>>>
	requests;
	// clang-format on

	bool stopped{ false };
	nano::condition_variable condition;
	mutable nano::mutex mutex{ mutex_identifier (mutexes::request_aggregator) };
	std::vector<std::thread> threads;

	friend std::unique_ptr<container_info_component> collect_container_info (request_aggregator &, std::string const &);
};
std::unique_ptr<container_info_component> collect_container_info (request_aggregator &, std::string const &);
}
