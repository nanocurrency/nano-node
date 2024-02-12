#include <nano/lib/jsonconfig.hpp>
#include <nano/lib/locks.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/stats_sinks.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/lib/tomlconfig.hpp>

#include <boost/format.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <ctime>
#include <fstream>
#include <sstream>

/*
 * stat_log_sink
 */

std::string nano::stat_log_sink::tm_to_string (tm & tm)
{
	return (boost::format ("%04d.%02d.%02d %02d:%02d:%02d") % (1900 + tm.tm_year) % (tm.tm_mon + 1) % tm.tm_mday % tm.tm_hour % tm.tm_min % tm.tm_sec).str ();
}

/*
 * stats
 */

nano::stats::stats (nano::stats_config config) :
	config{ std::move (config) }
{
}

nano::stats::~stats ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void nano::stats::start ()
{
	thread = std::thread ([this] {
		nano::thread_role::set (nano::thread_role::name::stats);
		run ();
	});
}

void nano::stats::stop ()
{
	{
		std::lock_guard guard{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	if (thread.joinable ())
	{
		thread.join ();
	}
}

void nano::stats::clear ()
{
	std::lock_guard guard{ mutex };
	counters.clear ();
	samplers.clear ();
	timestamp = std::chrono::steady_clock::now ();
}

void nano::stats::add (stat::type type, stat::detail detail, stat::dir dir, counter_value_t value)
{
	if (value == 0)
	{
		return;
	}

	// Updates need to happen while holding the mutex
	auto update_counter = [this] (nano::stats::counter_key key, auto && updater) {
		counter_key all_key{ key.type, stat::detail::all, key.dir };

		// This is a two-step process to avoid exclusively locking the mutex in the common case
		{
			std::shared_lock lock{ mutex };

			if (auto it = counters.find (key); it != counters.end ())
			{
				updater (*it->second);

				if (key != all_key)
				{
					auto it_all = counters.find (all_key);
					release_assert (it_all != counters.end ()); // The `all` counter should always be created together
					updater (*it_all->second); // Also update the `all` counter
				}

				return;
			}
		}
		// Not found, create a new entry
		{
			std::unique_lock lock{ mutex };

			// Insertions will be ignored if the key already exists
			auto [it, inserted] = counters.emplace (key, std::make_unique<counter_entry> ());
			auto [it_all, inserted_all] = counters.emplace (all_key, std::make_unique<counter_entry> ());

			updater (*it->second);

			if (key != all_key)
			{
				updater (*it_all->second); // Also update the `all` counter
			}
		}
	};

	update_counter (counter_key{ type, detail, dir }, [value] (counter_entry & counter) {
		counter.value += value;
	});
}

auto nano::stats::count (stat::type type, stat::detail detail, stat::dir dir) const -> counter_value_t
{
	std::shared_lock lock{ mutex };
	if (auto it = counters.find (counter_key{ type, detail, dir }); it != counters.end ())
	{
		return it->second->value;
	}
	return 0;
}

void nano::stats::sample (stat::type type, stat::sample sample, nano::stats::sampler_value_t value)
{
	// Updates need to happen while holding the mutex
	auto update_sampler = [this] (nano::stats::sampler_key key, auto && updater) {
		// This is a two-step process to avoid exclusively locking the mutex in the common case
		{
			std::shared_lock lock{ mutex };

			if (auto it = samplers.find (key); it != samplers.end ())
			{
				updater (*it->second);
				return;
			}
		}
		// Not found, create a new entry
		{
			std::unique_lock lock{ mutex };

			// Insertions will be ignored if the key already exists
			auto [it, inserted] = samplers.emplace (key, std::make_unique<sampler_entry> (config.max_samples));
			updater (*it->second);
		}
	};

	update_sampler (sampler_key{ type, sample }, [value] (sampler_entry & sampler) {
		sampler.add (value);
	});
}

auto nano::stats::samples (stat::type type, stat::sample sample) -> std::vector<sampler_value_t>
{
	std::shared_lock lock{ mutex };
	if (auto it = samplers.find (sampler_key{ type, sample }); it != samplers.end ())
	{
		return it->second->collect ();
	}
	return {};
}

void nano::stats::log_counters (stat_log_sink & sink)
{
	// TODO: Replace with a proper std::chrono time
	std::time_t time = std::chrono::system_clock::to_time_t (std::chrono::system_clock::now ());
	tm local_tm = *localtime (&time);

	std::lock_guard guard{ mutex };
	log_counters_impl (sink, local_tm);
}

void nano::stats::log_counters_impl (stat_log_sink & sink, tm & tm)
{
	sink.begin ();
	if (sink.entries () >= config.log_rotation_count)
	{
		sink.rotate ();
	}

	if (config.log_headers)
	{
		auto walltime (std::chrono::system_clock::now ());
		sink.write_header ("counters", walltime);
	}

	for (auto const & [key, entry] : counters)
	{
		std::string type{ to_string (key.type) };
		std::string detail{ to_string (key.detail) };
		std::string dir{ to_string (key.dir) };

		sink.write_counter_entry (tm, type, detail, dir, entry->value);
	}
	sink.entries ()++;
	sink.finalize ();
}

void nano::stats::log_samples (stat_log_sink & sink)
{
	// TODO: Replace with a proper std::chrono time
	std::time_t time = std::chrono::system_clock::to_time_t (std::chrono::system_clock::now ());
	tm local_tm = *localtime (&time);

	std::lock_guard guard{ mutex };
	log_samples_impl (sink, local_tm);
}

void nano::stats::log_samples_impl (stat_log_sink & sink, tm & tm)
{
	sink.begin ();
	if (sink.entries () >= config.log_rotation_count)
	{
		sink.rotate ();
	}

	if (config.log_headers)
	{
		auto walltime (std::chrono::system_clock::now ());
		sink.write_header ("samples", walltime);
	}

	for (auto const & [key, entry] : samplers)
	{
		std::string type{ to_string (key.type) };
		std::string sample{ to_string (key.sample) };

		sink.write_sampler_entry (tm, type, sample, entry->collect ());
	}

	sink.entries ()++;
	sink.finalize ();
}

std::chrono::milliseconds nano::stats::calculate_run_interval () const
{
	std::chrono::milliseconds interval = std::chrono::milliseconds::max ();
	if (config.log_counters_interval.count () > 0)
	{
		interval = std::min (interval, config.log_counters_interval);
	}
	if (config.log_samples_interval.count () > 0)
	{
		interval = std::min (interval, config.log_samples_interval);
	}
	return interval;
}

void nano::stats::run ()
{
	auto const interval = calculate_run_interval ();

	if (interval == std::chrono::milliseconds::max ())
	{
		return;
	}

	std::unique_lock lock{ mutex };
	while (!stopped)
	{
		condition.wait_for (lock, interval);

		if (!stopped)
		{
			run_one (lock);
			debug_assert (lock.owns_lock ());
		}
	}
}

void nano::stats::run_one (std::unique_lock<std::shared_mutex> & lock)
{
	static stat_file_writer log_count{ config.log_counters_filename };
	static stat_file_writer log_sample{ config.log_samples_filename };

	debug_assert (!mutex.try_lock ());
	debug_assert (lock.owns_lock ());

	// TODO: Replace with a proper std::chrono time
	std::time_t time = std::chrono::system_clock::to_time_t (std::chrono::system_clock::now ());
	tm local_tm = *localtime (&time);

	// Counters
	if (config.log_counters_interval.count () > 0)
	{
		if (nano::elapse (log_last_count_writeout, config.log_counters_interval))
		{
			log_counters_impl (log_count, local_tm);
		}
	}

	// Samples
	if (config.log_samples_interval.count () > 0)
	{
		if (nano::elapse (log_last_sample_writeout, config.log_samples_interval))
		{
			log_samples_impl (log_sample, local_tm);
		}
	}
}

std::chrono::seconds nano::stats::last_reset ()
{
	std::lock_guard guard{ mutex };
	auto now (std::chrono::steady_clock::now ());
	return std::chrono::duration_cast<std::chrono::seconds> (now - timestamp);
}

std::string nano::stats::dump (category category)
{
	stat_json_writer sink;
	switch (category)
	{
		case category::counters:
			log_counters (sink);
			break;
		case category::samples:
			log_samples (sink);
			break;
		default:
			debug_assert (false, "missing stat_category case");
	}
	return sink.to_string ();
}

/*
 * stats::sampler_entry
 */

void nano::stats::sampler_entry::add (nano::stats::sampler_value_t value)
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	samples.push_back (value);
}

auto nano::stats::sampler_entry::collect () -> std::vector<sampler_value_t>
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	std::vector<sampler_value_t> result{ samples.begin (), samples.end () };
	samples.clear ();
	return result;
}

/*
 * stats_config
 */

nano::error nano::stats_config::serialize_toml (nano::tomlconfig & toml) const
{
	toml.put ("max_samples", max_samples, "Maximum number of samples to keep in the ring buffer.\ntype:uint64");

	nano::tomlconfig log_l;
	log_l.put ("headers", log_headers, "If true, write headers on each counter or samples writeout.\nThe header contains log type and the current wall time.\ntype:bool");
	log_l.put ("interval_counters", log_counters_interval.count (), "How often to log counters. 0 disables logging.\ntype:milliseconds");
	log_l.put ("interval_samples", log_samples_interval.count (), "How often to log samples. 0 disables logging.\ntype:milliseconds");
	log_l.put ("rotation_count", log_rotation_count, "Maximum number of log outputs before rotating the file.\ntype:uint64");
	log_l.put ("filename_counters", log_counters_filename, "Log file name for counters.\ntype:string");
	log_l.put ("filename_samples", log_samples_filename, "Log file name for samples.\ntype:string");
	toml.put_child ("log", log_l);

	return toml.get_error ();
}

nano::error nano::stats_config::deserialize_toml (nano::tomlconfig & toml)
{
	toml.get ("max_samples", max_samples);

	if (auto maybe_log_l = toml.get_optional_child ("log"))
	{
		auto log_l = *maybe_log_l;

		log_l.get ("headers", log_headers);

		auto counters_interval_l = log_counters_interval.count ();
		log_l.get ("interval_counters", counters_interval_l);
		log_counters_interval = std::chrono::milliseconds{ counters_interval_l };

		auto samples_interval_l = log_samples_interval.count ();
		log_l.get ("interval_samples", samples_interval_l);
		log_samples_interval = std::chrono::milliseconds{ samples_interval_l };

		log_l.get ("rotation_count", log_rotation_count);
		log_l.get ("filename_counters", log_counters_filename);
		log_l.get ("filename_samples", log_samples_filename);

		// Don't allow specifying the same file name for counter and samples logs
		if (log_counters_filename == log_samples_filename)
		{
			toml.get_error ().set ("The statistics counter and samples config values must be different");
		}
	}

	return toml.get_error ();
}