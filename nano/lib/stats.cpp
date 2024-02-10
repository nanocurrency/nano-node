#include <nano/lib/jsonconfig.hpp>
#include <nano/lib/locks.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/tomlconfig.hpp>

#include <boost/format.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <ctime>
#include <fstream>
#include <sstream>

/*
 * stats_config
 */

nano::error nano::stats_config::deserialize_toml (nano::tomlconfig & toml)
{
	auto sampling_l (toml.get_optional_child ("sampling"));
	if (sampling_l)
	{
		sampling_l->get<bool> ("enable", sampling_enabled);
		sampling_l->get<size_t> ("capacity", capacity);
		sampling_l->get<size_t> ("interval", interval);
	}

	auto log_l (toml.get_optional_child ("log"));
	if (log_l)
	{
		log_l->get<bool> ("headers", log_headers);
		log_l->get<size_t> ("interval_counters", log_interval_counters);
		log_l->get<size_t> ("interval_samples", log_interval_samples);
		log_l->get<size_t> ("rotation_count", log_rotation_count);
		log_l->get<std::string> ("filename_counters", log_counters_filename);
		log_l->get<std::string> ("filename_samples", log_samples_filename);

		// Don't allow specifying the same file name for counter and samples logs
		if (log_counters_filename == log_samples_filename)
		{
			toml.get_error ().set ("The statistics counter and samples config values must be different");
		}
	}

	return toml.get_error ();
}

nano::error nano::stats_config::serialize_toml (nano::tomlconfig & toml) const
{
	nano::tomlconfig sampling_l;
	sampling_l.put ("enable", sampling_enabled, "Enable or disable sampling.\ntype:bool");
	sampling_l.put ("capacity", capacity, "How many sample intervals to keep in the ring buffer.\ntype:uint64");
	sampling_l.put ("interval", interval, "Sample interval.\ntype:milliseconds");
	toml.put_child ("sampling", sampling_l);

	nano::tomlconfig log_l;
	log_l.put ("headers", log_headers, "If true, write headers on each counter or samples writeout.\nThe header contains log type and the current wall time.\ntype:bool");
	log_l.put ("interval_counters", log_interval_counters, "How often to log counters. 0 disables logging.\ntype:milliseconds");
	log_l.put ("interval_samples", log_interval_samples, "How often to log samples. 0 disables logging.\ntype:milliseconds");
	log_l.put ("rotation_count", log_rotation_count, "Maximum number of log outputs before rotating the file.\ntype:uint64");
	log_l.put ("filename_counters", log_counters_filename, "Log file name for counters.\ntype:string");
	log_l.put ("filename_samples", log_samples_filename, "Log file name for samples.\ntype:string");
	toml.put_child ("log", log_l);
	return toml.get_error ();
}

/*
 * stat_log_sink
 */

std::string nano::stat_log_sink::tm_to_string (tm & tm)
{
	return (boost::format ("%04d.%02d.%02d %02d:%02d:%02d") % (1900 + tm.tm_year) % (tm.tm_mon + 1) % tm.tm_mday % tm.tm_hour % tm.tm_min % tm.tm_sec).str ();
}

namespace
{
/** JSON sink. The resulting JSON object is provided as both a property_tree::ptree (to_object) and a string (to_string) */
class json_writer : public nano::stat_log_sink
{
	boost::property_tree::ptree tree;
	boost::property_tree::ptree entries;

public:
	std::ostream & out () override
	{
		return sstr;
	}

	void begin () override
	{
		tree.clear ();
	}

	void write_header (std::string const & header, std::chrono::system_clock::time_point & walltime) override
	{
		std::time_t now = std::chrono::system_clock::to_time_t (walltime);
		tm tm = *localtime (&now);
		tree.put ("type", header);
		tree.put ("created", tm_to_string (tm));
	}

	void write_counter_entry (tm & tm, std::string const & type, std::string const & detail, std::string const & dir, uint64_t value) override
	{
		boost::property_tree::ptree entry;
		entry.put ("time", boost::format ("%02d:%02d:%02d") % tm.tm_hour % tm.tm_min % tm.tm_sec);
		entry.put ("type", type);
		entry.put ("detail", detail);
		entry.put ("dir", dir);
		entry.put ("value", value);
		entries.push_back (std::make_pair ("", entry));
	}

	void finalize () override
	{
		tree.add_child ("entries", entries);
	}

	void * to_object () override
	{
		return &tree;
	}

	std::string to_string () override
	{
		boost::property_tree::write_json (sstr, tree);
		return sstr.str ();
	}

private:
	std::ostringstream sstr;
};

/** File sink with rotation support. This writes one counter per line and does not include histogram values. */
class file_writer : public nano::stat_log_sink
{
public:
	std::ofstream log;
	std::string filename;

	explicit file_writer (std::string const & filename) :
		filename (filename)
	{
		log.open (filename.c_str (), std::ofstream::out);
	}

	virtual ~file_writer ()
	{
		log.close ();
	}

	std::ostream & out () override
	{
		return log;
	}

	void write_header (std::string const & header, std::chrono::system_clock::time_point & walltime) override
	{
		std::time_t now = std::chrono::system_clock::to_time_t (walltime);
		tm tm = *localtime (&now);
		log << header << "," << boost::format ("%04d.%02d.%02d %02d:%02d:%02d") % (1900 + tm.tm_year) % (tm.tm_mon + 1) % tm.tm_mday % tm.tm_hour % tm.tm_min % tm.tm_sec << std::endl;
	}

	void write_counter_entry (tm & tm, std::string const & type, std::string const & detail, std::string const & dir, uint64_t value) override
	{
		log << boost::format ("%02d:%02d:%02d") % tm.tm_hour % tm.tm_min % tm.tm_sec << "," << type << "," << detail << "," << dir << "," << value << std::endl;
	}

	void rotate () override
	{
		log.close ();
		log.open (filename.c_str (), std::ofstream::out);
		log_entries = 0;
	}
};
}

/*
 * stats
 */

nano::stats::stats (nano::stats_config config) :
	config (config)
{
}

void nano::stats::stop ()
{
	std::lock_guard guard{ mutex };
	stopped = true;
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
			auto [it, inserted] = samplers.emplace (key, std::make_unique<sampler_entry> ());
			updater (*it->second);
		}
	};

	update_sampler (sampler_key{ type, sample }, [this, value] (sampler_entry & sampler) {
		sampler.add (value, config.capacity);
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

std::unique_ptr<nano::stat_log_sink> nano::stats::log_sink_json () const
{
	return std::make_unique<json_writer> ();
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

	for (auto const & [key, value] : counters)
	{
		std::string type{ to_string (key.type) };
		std::string detail{ to_string (key.detail) };
		std::string dir{ to_string (key.dir) };

		sink.write_counter_entry (tm, type, detail, dir, value->value);
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

	for (auto const & [key, value] : samplers)
	{
		std::string type{ to_string (key.type) };
		std::string sample{ to_string (key.sample) };

		for (auto & datapoint : value->collect ())
		{
			sink.write_sampler_entry (tm, type, sample, datapoint);
		}
	}

	sink.entries ()++;
	sink.finalize ();
}

// TODO: Run periodically in a separate thread
void nano::stats::update ()
{
	static file_writer log_count (config.log_counters_filename);
	static file_writer log_sample (config.log_samples_filename);

	std::lock_guard guard{ mutex };
	if (!stopped)
	{
		auto has_interval_counter = [&] () {
			return config.log_interval_counters > 0;
		};
		auto has_sampling = [&] () {
			return config.sampling_enabled && config.interval > 0;
		};

		if (has_interval_counter () || has_sampling ())
		{
			auto now = std::chrono::steady_clock::now (); // Only sample clock if necessary as this impacts node performance due to frequent usage

			// TODO: Replace with a proper std::chrono time
			std::time_t time = std::chrono::system_clock::to_time_t (std::chrono::system_clock::now ());
			tm local_tm = *localtime (&time);

			// Counters
			if (has_interval_counter ())
			{
				std::chrono::duration<double, std::milli> duration = now - log_last_count_writeout;
				if (duration.count () > config.log_interval_counters)
				{
					log_counters_impl (log_count, local_tm);
					log_last_count_writeout = now;
				}
			}

			// Samples
			if (has_sampling ())
			{
				std::chrono::duration<double, std::milli> duration = now - log_last_sample_writeout;
				if (duration.count () > config.log_interval_samples)
				{
					log_samples_impl (log_sample, local_tm);
					log_last_sample_writeout = now;
				}
			}
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
	auto sink = log_sink_json ();
	switch (category)
	{
		case category::counters:
			log_counters (*sink);
			break;
		case category::samples:
			log_samples (*sink);
			break;
		default:
			debug_assert (false, "missing stat_category case");
			break;
	}
	return sink->to_string ();
}

/*
 * stats::sampler_entry
 */

void nano::stats::sampler_entry::add (nano::stats::sampler_value_t value, size_t max_samples)
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	samples.push_back (value);
	while (samples.size () > max_samples)
	{
		samples.pop_front ();
	}
}

auto nano::stats::sampler_entry::collect () -> std::vector<sampler_value_t>
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	std::vector<sampler_value_t> result{ samples.begin (), samples.end () };
	samples.clear ();
	return result;
}