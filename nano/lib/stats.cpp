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

	void write_entry (tm & tm, std::string const & type, std::string const & detail, std::string const & dir, uint64_t value, nano::stat_histogram * histogram) override
	{
		boost::property_tree::ptree entry;
		entry.put ("time", boost::format ("%02d:%02d:%02d") % tm.tm_hour % tm.tm_min % tm.tm_sec);
		entry.put ("type", type);
		entry.put ("detail", detail);
		entry.put ("dir", dir);
		entry.put ("value", value);
		if (histogram != nullptr)
		{
			boost::property_tree::ptree histogram_node;
			for (auto const & bin : histogram->get_bins ())
			{
				boost::property_tree::ptree bin_node;
				bin_node.put ("start_inclusive", bin.start_inclusive);
				bin_node.put ("end_exclusive", bin.end_exclusive);
				bin_node.put ("value", bin.value);

				std::time_t time = std::chrono::system_clock::to_time_t (bin.timestamp);
				struct tm local_tm = *localtime (&time);
				bin_node.put ("time", boost::format ("%02d:%02d:%02d") % local_tm.tm_hour % local_tm.tm_min % local_tm.tm_sec);
				histogram_node.push_back (std::make_pair ("", bin_node));
			}
			entry.put_child ("histogram", histogram_node);
		}
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

	void write_entry (tm & tm, std::string const & type, std::string const & detail, std::string const & dir, uint64_t value, nano::stat_histogram *) override
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

/*
 * stat_histogram
 */

nano::stat_histogram::stat_histogram (std::initializer_list<uint64_t> intervals_a, size_t bin_count_a)
{
	if (bin_count_a == 0)
	{
		debug_assert (intervals_a.size () > 1);
		uint64_t start_inclusive_l = *intervals_a.begin ();
		for (auto it = std::next (intervals_a.begin ()); it != intervals_a.end (); ++it)
		{
			uint64_t end_exclusive_l = *it;
			bins.emplace_back (start_inclusive_l, end_exclusive_l);
			start_inclusive_l = end_exclusive_l;
		}
	}
	else
	{
		debug_assert (intervals_a.size () == 2);
		uint64_t min_inclusive_l = *intervals_a.begin ();
		uint64_t max_exclusive_l = *std::next (intervals_a.begin ());

		auto domain_l = (max_exclusive_l - min_inclusive_l);
		auto bin_size_l = (domain_l + bin_count_a - 1) / bin_count_a;
		auto last_bin_size_l = (domain_l % bin_size_l);
		auto next_start_l = min_inclusive_l;

		for (size_t i = 0; i < bin_count_a; i++, next_start_l += bin_size_l)
		{
			bins.emplace_back (next_start_l, next_start_l + bin_size_l);
		}
		if (last_bin_size_l > 0)
		{
			bins.emplace_back (next_start_l, next_start_l + last_bin_size_l);
		}
	}
}

void nano::stat_histogram::add (uint64_t index_a, uint64_t addend_a)
{
	nano::lock_guard<nano::mutex> lk{ histogram_mutex };
	debug_assert (!bins.empty ());

	// The search for a bin is linear, but we're searching just a few
	// contiguous items which are likely to be in cache.
	bool found_l = false;
	for (auto & bin : bins)
	{
		if (index_a >= bin.start_inclusive && index_a < bin.end_exclusive)
		{
			bin.value += addend_a;
			bin.timestamp = std::chrono::system_clock::now ();
			found_l = true;
			break;
		}
	}

	// Clamp into first or last bin if no suitable bin was found
	if (!found_l)
	{
		if (index_a < bins.front ().start_inclusive)
		{
			bins.front ().value += addend_a;
		}
		else
		{
			bins.back ().value += addend_a;
		}
	}
}

std::vector<nano::stat_histogram::bin> nano::stat_histogram::get_bins () const
{
	nano::lock_guard<nano::mutex> lk{ histogram_mutex };
	return bins;
}

/*
 * stats
 */

nano::stats::stats (nano::stats_config config) :
	config (config)
{
}

std::shared_ptr<nano::stat_entry> nano::stats::get_entry (uint32_t key)
{
	return get_entry (key, config.interval, config.capacity);
}

std::shared_ptr<nano::stat_entry> nano::stats::get_entry (uint32_t key, size_t interval, size_t capacity)
{
	nano::unique_lock<nano::mutex> lock{ stat_mutex };
	return get_entry_impl (key, interval, capacity);
}

std::shared_ptr<nano::stat_entry> nano::stats::get_entry_impl (uint32_t key, size_t interval, size_t capacity)
{
	std::shared_ptr<nano::stat_entry> res;
	auto entry = entries.find (key);
	if (entry == entries.end ())
	{
		res = entries.emplace (key, std::make_shared<nano::stat_entry> (capacity, interval)).first->second;
	}
	else
	{
		res = entry->second;
	}

	return res;
}

std::unique_ptr<nano::stat_log_sink> nano::stats::log_sink_json () const
{
	return std::make_unique<json_writer> ();
}

void nano::stats::log_counters (stat_log_sink & sink)
{
	nano::unique_lock<nano::mutex> lock{ stat_mutex };
	log_counters_impl (sink);
}

void nano::stats::log_counters_impl (stat_log_sink & sink)
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

	for (auto & it : entries)
	{
		std::time_t time = std::chrono::system_clock::to_time_t (it.second->counter.get_timestamp ());
		tm local_tm = *localtime (&time);

		auto key = it.first;
		std::string type = type_to_string (key);
		std::string detail = detail_to_string (key);
		std::string dir = dir_to_string (key);
		sink.write_entry (local_tm, type, detail, dir, it.second->counter.get_value (), it.second->histogram.get ());
	}
	sink.entries ()++;
	sink.finalize ();
}

void nano::stats::log_samples (stat_log_sink & sink)
{
	nano::unique_lock<nano::mutex> lock{ stat_mutex };
	log_samples_impl (sink);
}

void nano::stats::log_samples_impl (stat_log_sink & sink)
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

	for (auto & it : entries)
	{
		auto key = it.first;
		std::string type = type_to_string (key);
		std::string detail = detail_to_string (key);
		std::string dir = dir_to_string (key);

		for (auto & datapoint : it.second->samples)
		{
			std::time_t time = std::chrono::system_clock::to_time_t (datapoint.get_timestamp ());
			tm local_tm = *localtime (&time);
			sink.write_entry (local_tm, type, detail, dir, datapoint.get_value (), nullptr);
		}
	}
	sink.entries ()++;
	sink.finalize ();
}

void nano::stats::define_histogram (stat::type type, stat::detail detail, stat::dir dir, std::initializer_list<uint64_t> intervals_a, size_t bin_count_a /*=0*/)
{
	auto entry (get_entry (key_of (type, detail, dir)));
	entry->histogram = std::make_unique<nano::stat_histogram> (intervals_a, bin_count_a);
}

void nano::stats::update_histogram (stat::type type, stat::detail detail, stat::dir dir, uint64_t index_a, uint64_t addend_a)
{
	auto entry (get_entry (key_of (type, detail, dir)));
	debug_assert (entry->histogram != nullptr);
	entry->histogram->add (index_a, addend_a);
}

nano::stat_histogram * nano::stats::get_histogram (stat::type type, stat::detail detail, stat::dir dir)
{
	auto entry (get_entry (key_of (type, detail, dir)));
	debug_assert (entry->histogram != nullptr);
	return entry->histogram.get ();
}

void nano::stats::update (uint32_t key_a, uint64_t value)
{
	static file_writer log_count (config.log_counters_filename);
	static file_writer log_sample (config.log_samples_filename);

	nano::unique_lock<nano::mutex> lock{ stat_mutex };
	if (!stopped)
	{
		auto entry (get_entry_impl (key_a, config.interval, config.capacity));
		auto has_interval_counter = [&] () {
			return config.log_interval_counters > 0;
		};
		auto has_sampling = [&] () {
			return config.sampling_enabled && entry->sample_interval > 0;
		};

		// Counters
		auto old (entry->counter.get_value ());
		entry->counter.add (value, has_sampling ()); // Only update timestamp when sampling is enabled as this has a performance impact
		entry->count_observers.notify (old, entry->counter.get_value ());
		if (has_interval_counter () || has_sampling ())
		{
			auto now = std::chrono::steady_clock::now (); // Only sample clock if necessary as this impacts node performance due to frequent usage
			if (has_interval_counter ())
			{
				std::chrono::duration<double, std::milli> duration = now - log_last_count_writeout;
				if (duration.count () > config.log_interval_counters)
				{
					log_counters_impl (log_count);
					log_last_count_writeout = now;
				}
			}

			// Samples
			if (has_sampling ())
			{
				entry->sample_current.add (value, false);

				std::chrono::duration<double, std::milli> duration = now - entry->sample_start_time;
				if (duration.count () > entry->sample_interval)
				{
					entry->sample_start_time = now;

					// Make a snapshot of samples for thread safety and to get a stable container
					entry->sample_current.set_timestamp (std::chrono::system_clock::now ());
					entry->samples.push_back (entry->sample_current);
					entry->sample_current.set_value (0);

					if (!entry->sample_observers.empty ())
					{
						auto snapshot (entry->samples);
						entry->sample_observers.notify (snapshot);
					}

					// Log sink
					duration = now - log_last_sample_writeout;
					if (config.log_interval_samples > 0 && duration.count () > config.log_interval_samples)
					{
						log_samples_impl (log_sample);
						log_last_sample_writeout = now;
					}
				}
			}
		}
	}
}

std::chrono::seconds nano::stats::last_reset ()
{
	nano::unique_lock<nano::mutex> lock{ stat_mutex };
	auto now (std::chrono::steady_clock::now ());
	return std::chrono::duration_cast<std::chrono::seconds> (now - timestamp);
}

void nano::stats::stop ()
{
	nano::lock_guard<nano::mutex> guard{ stat_mutex };
	stopped = true;
}

std::string nano::stats::dump (nano::stat_category category)
{
	auto sink = log_sink_json ();
	switch (category)
	{
		case nano::stat_category::counters:
			log_counters (*sink);
			break;
		case nano::stat_category::samples:
			log_samples (*sink);
			break;
		default:
			debug_assert (false, "missing stat_category case");
			break;
	}
	return sink->to_string ();
}

void nano::stats::clear ()
{
	nano::unique_lock<nano::mutex> lock{ stat_mutex };
	entries.clear ();
	timestamp = std::chrono::steady_clock::now ();
}

std::string nano::stats::type_to_string (uint32_t key)
{
	auto type = static_cast<stat::type> (key >> 16 & 0x000000ff);
	return std::string{ nano::to_string (type) };
}

std::string nano::stats::detail_to_string (uint32_t key)
{
	auto detail = static_cast<stat::detail> (key >> 8 & 0x000000ff);
	return std::string{ nano::to_string (detail) };
}

std::string nano::stats::dir_to_string (uint32_t key)
{
	auto dir = static_cast<stat::dir> (key & 0x000000ff);
	return std::string{ nano::to_string (dir) };
}

/*
 * stat_datapoint
 */

nano::stat_datapoint::stat_datapoint (stat_datapoint const & other_a)
{
	nano::lock_guard<nano::mutex> lock{ other_a.datapoint_mutex };
	value = other_a.value;
	timestamp = other_a.timestamp;
}

nano::stat_datapoint & nano::stat_datapoint::operator= (stat_datapoint const & other_a)
{
	nano::lock_guard<nano::mutex> lock{ other_a.datapoint_mutex };
	value = other_a.value;
	timestamp = other_a.timestamp;
	return *this;
}

uint64_t nano::stat_datapoint::get_value () const
{
	nano::lock_guard<nano::mutex> lock{ datapoint_mutex };
	return value;
}

void nano::stat_datapoint::set_value (uint64_t value_a)
{
	nano::lock_guard<nano::mutex> lock{ datapoint_mutex };
	value = value_a;
}

std::chrono::system_clock::time_point nano::stat_datapoint::get_timestamp () const
{
	nano::lock_guard<nano::mutex> lock{ datapoint_mutex };
	return timestamp;
}

void nano::stat_datapoint::set_timestamp (std::chrono::system_clock::time_point timestamp_a)
{
	nano::lock_guard<nano::mutex> lock{ datapoint_mutex };
	timestamp = timestamp_a;
}

/** Add \addend to the current value and optionally update the timestamp */
void nano::stat_datapoint::add (uint64_t addend, bool update_timestamp)
{
	nano::lock_guard<nano::mutex> lock{ datapoint_mutex };
	value += addend;
	if (update_timestamp)
	{
		timestamp = std::chrono::system_clock::now ();
	}
}
