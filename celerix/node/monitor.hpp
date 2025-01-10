#pragma once

#include <celerix/lib/locks.hpp>
#include <celerix/node/fwd.hpp>

#include <chrono>
#include <thread>

using namespace std::chrono_literals;

namespace celerix
{
class monitor_config final
{
public:
	celerix::error deserialize (celerix::tomlconfig &);
	celerix::error serialize (celerix::tomlconfig &) const;

public:
	bool enable{ true };
	std::chrono::seconds interval{ 60s };
};

class monitor final
{
public:
	monitor (monitor_config const &, celerix::node &);
	~monitor ();

	void start ();
	void stop ();

private: // Dependencies
	monitor_config const & config;
	celerix::node & node;
	celerix::logger & logger;

private:
	void run ();
	void run_one ();

	std::chrono::steady_clock::time_point last_time{};

	size_t last_blocks_cemented{ 0 };
	size_t last_blocks_total{ 0 };

	bool stopped{ false };
	celerix::condition_variable condition;
	mutable celerix::mutex mutex;
	std::thread thread;
};
}