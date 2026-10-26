#pragma once

#include <nano/lib/interval.hpp>
#include <nano/lib/locks.hpp>
#include <nano/lib/threading.hpp>
#include <nano/node/fair_queue_traits.hpp>
#include <nano/node/fwd.hpp>

#include <thread>
#include <vector>

namespace nano
{
class message_processor_config final
{
public:
	nano::error deserialize (nano::tomlconfig & toml);
	nano::error serialize (nano::tomlconfig & toml) const;

public:
	size_t threads{ std::clamp (nano::hardware_concurrency () / 4, 1u, 2u) }; // Threads draining the queue
	size_t max_queue{ 64 }; // Messages queued per channel before further ones are dropped
};

/*
 * Queues the messages peers send and hands them to the dispatcher on a pool of threads.
 * If mutex locking is ever a performance bottleneck, using a lock-free queue in front of the priority queue should be considered.
 */
class message_processor final
{
public:
	message_processor (message_processor_config const &, nano::message_dispatcher &, nano::stats &, nano::logger &);
	~message_processor ();

	void start ();
	void stop ();

	// Queues a message from the channel for processing, false if the channel's queue is full and it was dropped
	bool enqueue (std::unique_ptr<nano::messages::message> message, std::shared_ptr<nano::transport::channel> const & channel);
	// Hands a message to the dispatcher on the calling thread
	void process (nano::messages::message const & message, std::shared_ptr<nano::transport::channel> const & channel);

	nano::container_info container_info () const;

private:
	void run ();
	void run_batch (nano::unique_lock<nano::mutex> &);

private: // Dependencies
	message_processor_config const & config;
	nano::message_dispatcher & dispatcher;
	nano::stats & stats;
	nano::logger & logger;

private:
	using entry_t = std::pair<std::unique_ptr<nano::messages::message>, std::shared_ptr<nano::transport::channel>>;
	nano::fair_queue<entry_t, nano::no_value, std::shared_ptr<nano::transport::channel>> queue;

	std::atomic<bool> stopped{ false };
	mutable nano::mutex mutex;
	nano::condition_variable condition;
	std::vector<std::thread> threads;

	nano::interval log_interval;
};
}
