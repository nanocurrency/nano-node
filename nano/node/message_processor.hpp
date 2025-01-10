#pragma once

#include <celerix/lib/locks.hpp>
#include <celerix/lib/threading.hpp>
#include <celerix/node/fair_queue.hpp>
#include <celerix/node/fwd.hpp>

#include <thread>
#include <vector>

namespace celerix
{
class message_processor_config final
{
public:
	celerix::error deserialize (celerix::tomlconfig & toml);
	celerix::error serialize (celerix::tomlconfig & toml) const;

public:
	size_t threads{ std::clamp (celerix::hardware_concurrency () / 4, 1u, 2u) };
	size_t max_queue{ 64 };
};

/*
 * If mutex locking is ever a performance bottleneck, using a lock-free queue in front of the priority queue should be considered.
 */
class message_processor final
{
public:
	explicit message_processor (message_processor_config const &, celerix::node &);
	~message_processor ();

	void start ();
	void stop ();

	bool put (std::unique_ptr<celerix::message>, std::shared_ptr<celerix::transport::channel> const &);
	void process (celerix::message const &, std::shared_ptr<celerix::transport::channel> const &);

	celerix::container_info container_info () const;

private:
	void run ();
	void run_batch (celerix::unique_lock<celerix::mutex> &);

private: // Dependencies
	message_processor_config const & config;
	celerix::node & node;
	celerix::stats & stats;
	celerix::logger & logger;

private:
	using entry_t = std::pair<std::unique_ptr<celerix::message>, std::shared_ptr<celerix::transport::channel>>;
	celerix::fair_queue<entry_t, celerix::no_value> queue;

	std::atomic<bool> stopped{ false };
	mutable celerix::mutex mutex;
	celerix::condition_variable condition;
	std::vector<std::thread> threads;
};
}