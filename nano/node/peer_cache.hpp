#pragma once

#include <nano/lib/locks.hpp>
#include <nano/node/common.hpp>
#include <nano/node/fwd.hpp>

#include <atomic>
#include <chrono>
#include <thread>

namespace nano
{
class peer_cache_config final
{
public:
	explicit peer_cache_config (nano::network_constants const & network);

	nano::error deserialize (nano::tomlconfig & toml);
	nano::error serialize (nano::tomlconfig & toml) const;

public:
	std::chrono::seconds erase_cutoff{ 60 * 60s };
	std::chrono::seconds check_interval{ 15s };
};

class peer_cache final
{
public:
	peer_cache (peer_cache_config const &, nano::store::component &, nano::network &, nano::logger &, nano::stats &);
	~peer_cache ();

	void start ();
	void stop ();

	std::vector<nano::endpoint> cached_peers () const;
	bool exists (nano::endpoint const & endpoint) const;
	size_t size () const;
	void trigger ();

private:
	void run ();
	void run_one ();

private: // Dependencies
	peer_cache_config const & config;
	nano::store::component & store;
	nano::network & network;
	nano::logger & logger;
	nano::stats & stats;

private:
	std::atomic<bool> stopped{ false };
	mutable nano::mutex mutex;
	nano::condition_variable condition;
	std::thread thread;
};
}