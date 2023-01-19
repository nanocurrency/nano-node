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
	 * Base class used as a base to build counters
	 */
	class counter
	{
	public:
		/*
		 * Calculate count and time delta since last call
		 */
		std::pair<uint64_t, std::chrono::milliseconds> observe ();

		virtual uint64_t count () = 0;
		virtual std::string name () = 0;

	private:
		std::chrono::system_clock::time_point last_observation{};
	};

	/*
	 * Counter that uses node stat container to provide info about rate
	 */
	class stat_counter final : public counter
	{
	public:
		explicit stat_counter (nano::stat & stats, nano::stat::type type, nano::stat::detail detail, nano::stat::dir dir);

		uint64_t count () override;
		std::string name () override;

	private:
		const nano::stat::type type;
		const nano::stat::detail detail;
		const nano::stat::dir dir;

		uint64_t last_count{ 0 };

		nano::stat & stats;
	};

public:
	rate_observer () = default;
	~rate_observer ();

	/*
	 * Periodically prints all observed rates onto the standard output
	 */
	void background_print (std::chrono::seconds interval);

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