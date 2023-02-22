#pragma once

#include <nano/lib/stats.hpp>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace nano::test
{
class rate_observer
{
public:
	/*
	 * Used as a base to build counters
	 */
	class counter final
	{
	public:
		using value_t = int64_t;

		struct observation
		{
			value_t total;
			value_t delta;
			std::chrono::milliseconds time_delta;
		};

	public:
		/*
		 * Calculate value total, value delta and time delta since last call
		 */
		observation observe ();

		explicit counter (std::string name, std::function<value_t ()> count);

		const std::string name;
		const std::function<value_t ()> count;

	private:
		std::chrono::system_clock::time_point last_observation{};
		value_t last_count{ 0 };
	};

public:
	rate_observer () = default;
	~rate_observer ();

	/*
	 * Periodically prints all observed rates onto the standard output
	 */
	void background_print (std::chrono::seconds interval);

	void observe (std::string name, std::function<int64_t ()> count);
	/*
	 * Starts observing a particular node stat from stat container
	 */
	void observe (nano::node &, nano::stat::type type, nano::stat::detail detail, nano::stat::dir dir);

private:
	void background_print_impl (std::chrono::seconds interval);
	void print_once ();

	std::vector<std::shared_ptr<counter>> counters;

	std::atomic<bool> stopped{ false };
	std::thread thread;
};
}