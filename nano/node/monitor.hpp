#pragma once

#include <nano/lib/locks.hpp>
#include <nano/node/fwd.hpp>

#include <chrono>
#include <thread>

using namespace std::chrono_literals;

namespace nano
{
class monitor_config final
{
public:
	nano::error deserialize (nano::tomlconfig &);
	nano::error serialize (nano::tomlconfig &) const;

public:
	bool enabled{ true };
	std::chrono::seconds interval{ 60s };
};

class monitor final
{
public:
	monitor (monitor_config const &, nano::node &);
	~monitor ();

	void start ();
	void stop ();

private: // Dependencies
	monitor_config const & config;
	nano::node & node;
	nano::logger & logger;

private:
	void run ();
	void run_one ();

	std::chrono::steady_clock::time_point last_time{};

	size_t last_blocks_cemented{ 0 };
	size_t last_blocks_total{ 0 };

	bool stopped{ false };
	nano::condition_variable condition;
	mutable nano::mutex mutex;
	std::thread thread;
};
}