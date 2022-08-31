#include <nano/lib/stats.hpp>
#include <nano/node/node.hpp>
#include <nano/test_common/rate_observer.hpp>
/*
 * rate_observer::counter
 */

std::pair<uint64_t, std::chrono::milliseconds> nano::test::rate_observer::counter::observe ()
{
	auto now = std::chrono::system_clock::now ();
	if (last_observation.time_since_epoch ().count () > 0)
	{
		auto time_delta = std::chrono::duration_cast<std::chrono::milliseconds> (now - last_observation);
		last_observation = now;
		return { count (), time_delta };
	}
	else
	{
		last_observation = now;
		return { 0, std::chrono::milliseconds{ 0 } };
	}
}

nano::test::rate_observer::stat_counter::stat_counter (nano::stat & stats_a, nano::stat::type type_a, nano::stat::detail detail_a, nano::stat::dir dir_a) :
	stats{ stats_a },
	type{ type_a },
	detail{ detail_a },
	dir{ dir_a }
{
}

/*
 * rate_observer::stat_counter
 */

uint64_t nano::test::rate_observer::stat_counter::count ()
{
	uint64_t cnt = stats.count (type, detail, dir);
	uint64_t delta = cnt - last_count;
	last_count = cnt;
	return delta;
}

std::string nano::test::rate_observer::stat_counter::name ()
{
	return nano::stat::type_to_string (type) + "::" + nano::stat::detail_to_string (detail) + "::" + nano::stat::dir_to_string (dir);
}

/*
 * rate_observer
 */

nano::test::rate_observer::~rate_observer ()
{
	if (!stopped.exchange (true))
	{
		if (thread.joinable ())
		{
			thread.join ();
		}
	}
}

void nano::test::rate_observer::background_print (std::chrono::seconds interval)
{
	release_assert (!thread.joinable ());
	thread = std::thread{ [this, interval] () { background_print_impl (interval); } };
}

void nano::test::rate_observer::background_print_impl (std::chrono::seconds interval)
{
	while (!stopped)
	{
		print_once ();

		std::this_thread::sleep_for (interval);
	}
}

void nano::test::rate_observer::print_once ()
{
	for (auto & counter : counters)
	{
		const auto observation = counter->observe ();

		// Convert delta milliseconds to seconds (double precision) and then divide the counter delta to get rate per second
		auto per_sec = observation.first / (observation.second.count () / 1000.0);

		std::cout << "rate of '" << counter->name () << "': "
				  << std::setw (12) << std::setprecision (2) << std::fixed << per_sec << " /s"
				  << std::endl;
	}
}

void nano::test::rate_observer::observe (nano::node & node, nano::stat::type type, nano::stat::detail detail, nano::stat::dir dir)
{
	auto counter = std::make_shared<stat_counter> (node.stats, type, detail, dir);
	counters.push_back (counter);
}
