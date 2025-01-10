#pragma once

#include <celerix/lib/locks.hpp>
#include <celerix/node/endpoint.hpp>
#include <celerix/node/fwd.hpp>

#include <atomic>
#include <chrono>
#include <thread>

namespace celerix
{
class peer_history_config final
{
public:
	explicit peer_history_config (celerix::network_constants const & network);

	celerix::error deserialize (celerix::tomlconfig & toml);
	celerix::error serialize (celerix::tomlconfig & toml) const;

public:
	std::chrono::seconds erase_cutoff{ 60 * 60s };
	std::chrono::seconds check_interval{ 15s };
};

class peer_history final
{
public:
	peer_history (peer_history_config const &, celerix::store::component &, celerix::network &, celerix::logger &, celerix::stats &);
	~peer_history ();

	void start ();
	void stop ();

	std::vector<celerix::endpoint> peers () const;
	bool exists (celerix::endpoint const & endpoint) const;
	size_t size () const;
	void trigger ();

private:
	void run ();
	void run_one ();

private: // Dependencies
	peer_history_config const & config;
	celerix::store::component & store;
	celerix::network & network;
	celerix::logger & logger;
	celerix::stats & stats;

private:
	std::atomic<bool> stopped{ false };
	mutable celerix::mutex mutex;
	celerix::condition_variable condition;
	std::thread thread;
};
}
