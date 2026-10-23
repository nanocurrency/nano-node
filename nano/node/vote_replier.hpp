#pragma once

#include <nano/lib/interval.hpp>
#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/threading.hpp>
#include <nano/node/fair_queue.hpp>
#include <nano/node/fwd.hpp>
#include <nano/node/transport/channel.hpp>
#include <nano/node/transport/transport.hpp>

#include <atomic>
#include <condition_variable>
#include <thread>
#include <vector>

namespace nano
{
class vote_replier_config final
{
public:
	nano::error deserialize (nano::tomlconfig &);
	nano::error serialize (nano::tomlconfig &) const;

public:
	size_t threads{ std::clamp (nano::hardware_concurrency () / 2, 1u, 4u) };
	size_t channel_limit{ 128 };
	size_t batch_size{ 16 };
};

/**
 * Handles vote reply requests from the network.
 * Receives confirmation requests, looks up blocks, checks vote eligibility,
 * signs votes and sends replies directly back to the requesting channel.
 */
class vote_replier final
{
public:
	vote_replier (vote_replier_config const &, nano::voting_policy &, nano::ledger &, nano::wallet::wallets &, nano::network_constants const &, nano::stats &, nano::logger &, bool enable_voting);
	~vote_replier ();

	void start ();
	void stop ();

	using request_type = std::vector<std::pair<nano::block_hash, nano::root>>;

	bool request (request_type const & request, std::shared_ptr<nano::transport::channel> const &);

	std::size_t size () const;
	bool empty () const;

	nano::container_info container_info () const;

private:
	void run ();
	void run_batch (nano::unique_lock<nano::mutex> & lock);
	void process (nano::secure::transaction const &, request_type const &, std::shared_ptr<nano::transport::channel> const &);

private: // Dependencies
	vote_replier_config const & config;
	nano::voting_policy & policy;
	nano::ledger & ledger;
	nano::wallet::wallets & wallets;
	nano::network_constants const & network_constants;
	nano::stats & stats;
	nano::logger & logger;

private:
	using value_type = std::pair<request_type, std::shared_ptr<nano::transport::channel>>;
	nano::fair_queue<value_type, nano::no_value, std::shared_ptr<nano::transport::channel>> queue;

	bool const enabled;
	bool stopped{ false };
	nano::condition_variable condition;
	mutable nano::mutex mutex{ mutex_identifier (mutexes::vote_replier) };
	std::vector<std::thread> threads;

	nano::interval log_interval;
};
}
