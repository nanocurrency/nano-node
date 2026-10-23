#pragma once

#include <nano/lib/function.hpp>
#include <nano/lib/interval.hpp>
#include <nano/lib/locks.hpp>
#include <nano/node/fwd.hpp>
#include <nano/node/transport/common.hpp>

#include <boost/asio/ip/tcp.hpp>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <unordered_map>

namespace nano
{
// Reaches out to peers found in keepalives, in the peer history and in the preconfigured list
class network_reachout final
{
public:
	explicit network_reachout (nano::node &);
	~network_reachout ();

	void start ();
	void stop ();

	// Resolves the address and connects to each of its endpoints that has no channel yet
	void reachout (std::string const & address, uint16_t port, nano::transport::connect_callback callback = noop<nano::transport::connect_callback> ());
	// Reaches out to every preconfigured peer
	void reachout_preconfigured ();
	// Makes the preconfigured loop reach out now instead of at its next interval
	void trigger ();

private:
	void run_live ();
	void run_cached ();
	void run_preconfigured ();

private: // Dependencies
	nano::node & node;

private:
	boost::asio::ip::tcp::resolver resolver;

private: // Preconfigured reachout
	// Spaces the rounds a warm-up or a full period apart, reset by a trigger
	nano::interval_mt preconfigured_interval;

	// Set by a trigger and cleared by the loop, under mutex
	bool preconfigured_triggered{ false };

	// When each peer's connect failure and success were last logged, so each is logged at most once an hour
	struct preconfigured_log
	{
		std::chrono::steady_clock::time_point last_success{};
		std::chrono::steady_clock::time_point last_failure{};

		bool should_log (bool success);
	};

	// Keyed by the configured peer name, so it holds at most one entry per preconfigured peer
	nano::locked<std::unordered_map<std::string, preconfigured_log>> preconfigured_logs;

private:
	std::atomic<bool> stopped{ false };
	nano::mutex mutex;
	nano::condition_variable condition;
	std::thread live_thread;
	std::thread cached_thread;
	std::thread preconfigured_thread;
};
}
