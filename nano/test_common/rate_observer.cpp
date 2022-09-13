#include <nano/lib/stats.hpp>
#include <nano/node/node.hpp>
#include <nano/test_common/rate_observer.hpp>

#include <utility>

nano::test::rate_observer::counter::counter (std::string name_a, std::function<value_t ()> count_a) :
	name{ std::move (name_a) },
	count{ std::move (count_a) }
{
}

nano::test::rate_observer::counter::observation nano::test::rate_observer::counter::observe ()
{
	auto now = std::chrono::system_clock::now ();
	auto total = count ();
	if (last_observation.time_since_epoch ().count () > 0)
	{
		auto time_delta = std::chrono::duration_cast<std::chrono::milliseconds> (now - last_observation);
		last_observation = now;
		auto delta = total - last_count;
		last_count = total;
		return { total, delta, time_delta };
	}
	else
	{
		last_observation = now;
		last_count = total;
		return { 0, 0, std::chrono::milliseconds{ 0 } };
	}
}

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
		auto per_sec = observation.delta / (observation.time_delta.count () / 1000.0);

		std::cout << "rate of '" << counter->name << "': "
				  << std::setw (12) << std::setprecision (2) << std::fixed << per_sec << " /s"
				  << std::endl;
	}
}

void nano::test::rate_observer::observe (std::string name, std::function<int64_t ()> observe)
{
	auto counter_instance = std::make_shared<counter> (name, observe);
	counters.push_back (counter_instance);
}

void nano::test::rate_observer::observe (nano::node & node, nano::stat::type type, nano::stat::detail detail, nano::stat::dir dir)
{
	auto name = nano::stat::type_to_string (type) + "::" + nano::stat::detail_to_string (detail) + "::" + nano::stat::dir_to_string (dir);

	observe (name, [&node, type, detail, dir] () {
		return node.stats.count (type, detail, dir);
	});
}
