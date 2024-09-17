#pragma once

#include <nano/lib/blocks.hpp>
#include <nano/lib/locks.hpp>
#include <nano/lib/processing_queue.hpp>
#include <nano/lib/rate_limiting.hpp>
#include <nano/node/blockprocessor.hpp>
#include <nano/node/fwd.hpp>
#include <nano/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <memory>
#include <thread>
#include <unordered_set>

namespace mi = boost::multi_index;

namespace nano
{
class local_block_broadcaster_config final
{
public:
	explicit local_block_broadcaster_config (nano::network_constants const & network)
	{
		if (network.is_dev_network ())
		{
			rebroadcast_interval = 1s;
			cleanup_interval = 1s;
		}
	}

	// TODO: Serialization & deserialization

public:
	std::size_t max_size{ 1024 * 8 };
	std::chrono::seconds rebroadcast_interval{ 3 };
	std::chrono::seconds max_rebroadcast_interval{ 60 };
	std::size_t broadcast_rate_limit{ 32 };
	double broadcast_rate_burst_ratio{ 3 };
	std::chrono::seconds cleanup_interval{ 60 };
};

/**
 * Broadcasts blocks to the network
 * Tracks local blocks for more aggressive propagation
 */
class local_block_broadcaster final
{
public:
	local_block_broadcaster (local_block_broadcaster_config const &, nano::node &, nano::block_processor &, nano::network &, nano::confirming_set &, nano::stats &, nano::logger &, bool enabled = false);
	~local_block_broadcaster ();

	void start ();
	void stop ();

	size_t size () const;

	std::unique_ptr<container_info_component> collect_container_info (std::string const & name) const;

private:
	void run ();
	void run_broadcasts (nano::unique_lock<nano::mutex> &);
	void cleanup (nano::unique_lock<nano::mutex> &);
	std::chrono::milliseconds rebroadcast_interval (unsigned rebroadcasts) const;

private: // Dependencies
	local_block_broadcaster_config const & config;
	nano::node & node;
	nano::block_processor & block_processor;
	nano::network & network;
	nano::confirming_set & confirming_set;
	nano::stats & stats;
	nano::logger & logger;

private:
	struct local_entry
	{
		std::shared_ptr<nano::block> block;
		std::chrono::steady_clock::time_point arrival;

		std::chrono::steady_clock::time_point last_broadcast{};
		std::chrono::steady_clock::time_point next_broadcast{};
		unsigned rebroadcasts{ 0 };

		nano::block_hash hash () const
		{
			return block->hash ();
		}
	};

	// clang-format off
	class tag_sequenced	{};
	class tag_hash {};
	class tag_broadcast {};

	using ordered_locals = boost::multi_index_container<local_entry,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_unique<mi::tag<tag_hash>,
			mi::const_mem_fun<local_entry, nano::block_hash, &local_entry::hash>>,
		mi::ordered_non_unique<mi::tag<tag_broadcast>,
			mi::member<local_entry, std::chrono::steady_clock::time_point, &local_entry::next_broadcast>>
	>>;
	// clang-format on

	ordered_locals local_blocks;

private:
	bool enabled{ false };
	nano::rate_limiter limiter;
	nano::interval cleanup_interval;

	std::atomic<bool> stopped{ false };
	nano::condition_variable condition;
	mutable nano::mutex mutex;
	std::thread thread;
};
}
