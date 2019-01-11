#include <nano/node/stats.hpp>

#include <boost/asio.hpp>
#include <boost/format.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>
#include <tuple>

bool nano::stat_config::deserialize_json (boost::property_tree::ptree & tree_a)
{
	bool error = false;

	auto sampling_l (tree_a.get_child_optional ("sampling"));
	if (sampling_l)
	{
		sampling_enabled = sampling_l->get<bool> ("enabled", sampling_enabled);
		capacity = sampling_l->get<size_t> ("capacity", capacity);
		interval = sampling_l->get<size_t> ("interval", interval);
	}

	auto log_l (tree_a.get_child_optional ("log"));
	if (log_l)
	{
		log_headers = log_l->get<bool> ("headers", log_headers);
		log_interval_counters = log_l->get<size_t> ("interval_counters", log_interval_counters);
		log_interval_samples = log_l->get<size_t> ("interval_samples", log_interval_samples);
		log_rotation_count = log_l->get<size_t> ("rotation_count", log_rotation_count);
		log_counters_filename = log_l->get<std::string> ("filename_counters", log_counters_filename);
		log_samples_filename = log_l->get<std::string> ("filename_samples", log_samples_filename);

		// Don't allow specifying the same file name for counter and samples logs
		error = (log_counters_filename == log_samples_filename);
	}

	return error;
}

std::string nano::stat_log_sink::tm_to_string (tm & tm_a)
{
	return (boost::format ("%04d.%02d.%02d %02d:%02d:%02d") % (1900 + tm_a.tm_year) % (tm_a.tm_mon + 1) % tm_a.tm_mday % tm_a.tm_hour % tm_a.tm_min % tm_a.tm_sec).str ();
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

	void write_header (std::string header_a, std::chrono::system_clock::time_point & walltime_a) override
	{
		std::time_t now = std::chrono::system_clock::to_time_t (walltime_a);
		tm tm = *localtime (&now);
		tree.put ("type", header_a);
		tree.put ("created", tm_to_string (tm));
	}

	void write_entry (tm & tm_a, std::string type_a, std::string detail_a, std::string dir_a, uint64_t value_a) override
	{
		boost::property_tree::ptree entry;
		entry.put ("time", boost::format ("%02d:%02d:%02d") % tm_a.tm_hour % tm_a.tm_min % tm_a.tm_sec);
		entry.put ("type", type_a);
		entry.put ("detail", detail_a);
		entry.put ("dir", dir_a);
		entry.put ("value", value_a);
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

/** File sink with rotation support */
class file_writer : public nano::stat_log_sink
{
public:
	std::ofstream log;
	std::string filename;

	file_writer (std::string filename_a) :
	filename (filename_a)
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

	void write_header (std::string header_a, std::chrono::system_clock::time_point & walltime_a) override
	{
		std::time_t now = std::chrono::system_clock::to_time_t (walltime_a);
		tm tm_l = *localtime (&now);
		log << header_a << "," << boost::format ("%04d.%02d.%02d %02d:%02d:%02d") % (1900 + tm_l.tm_year) % (tm_l.tm_mon + 1) % tm_l.tm_mday % tm_l.tm_hour % tm_l.tm_min % tm_l.tm_sec << std::endl;
	}

	void write_entry (tm & tm_a, std::string type_a, std::string detail_a, std::string dir_a, uint64_t value_a) override
	{
		log << boost::format ("%02d:%02d:%02d") % tm_a.tm_hour % tm_a.tm_min % tm_a.tm_sec << "," << type_a << "," << detail_a << "," << dir_a << "," << value_a << std::endl;
	}

	void rotate () override
	{
		log.close ();
		log.open (filename.c_str (), std::ofstream::out);
		log_entries = 0;
	}
};

nano::stat::stat (nano::stat_config config_a) :
config (config_a)
{
}

std::shared_ptr<nano::stat_entry> nano::stat::get_entry (uint32_t key_a)
{
	return get_entry (key_a, config.interval, config.capacity);
}

std::shared_ptr<nano::stat_entry> nano::stat::get_entry (uint32_t key_a, size_t interval_a, size_t capacity)
{
	std::unique_lock<std::mutex> lock (stat_mutex);
	return get_entry_impl (key_a, interval_a, capacity);
}

std::shared_ptr<nano::stat_entry> nano::stat::get_entry_impl (uint32_t key_a, size_t interval_a, size_t capacity_a)
{
	std::shared_ptr<nano::stat_entry> res;
	auto entry = entries.find (key_a);
	if (entry == entries.end ())
	{
		res = entries.insert (std::make_pair (key_a, std::make_shared<nano::stat_entry> (capacity_a, interval_a))).first->second;
	}
	else
	{
		res = entry->second;
	}

	return res;
}

std::unique_ptr<nano::stat_log_sink> nano::stat::log_sink_json ()
{
	return std::make_unique<json_writer> ();
}

std::unique_ptr<nano::stat_log_sink> log_sink_file (std::string filename_a)
{
	return std::make_unique<file_writer> (filename_a);
}

void nano::stat::log_counters (stat_log_sink & sink_a)
{
	std::unique_lock<std::mutex> lock (stat_mutex);
	log_counters_impl (sink_a);
}

void nano::stat::log_counters_impl (stat_log_sink & sink_a)
{
	sink_a.begin ();
	if (sink_a.entries () >= config.log_rotation_count)
	{
		sink_a.rotate ();
	}

	if (config.log_headers)
	{
		auto walltime (std::chrono::system_clock::now ());
		sink_a.write_header ("counters", walltime);
	}

	for (auto & it : entries)
	{
		std::time_t time = std::chrono::system_clock::to_time_t (it.second->counter.timestamp);
		tm local_tm = *localtime (&time);

		auto key = it.first;
		std::string type = type_to_string (key);
		std::string detail = detail_to_string (key);
		std::string dir = dir_to_string (key);
		sink_a.write_entry (local_tm, type, detail, dir, it.second->counter.value);
	}
	sink_a.entries ()++;
	sink_a.finalize ();
}

void nano::stat::log_samples (stat_log_sink & sink_a)
{
	std::unique_lock<std::mutex> lock (stat_mutex);
	log_samples_impl (sink_a);
}

void nano::stat::log_samples_impl (stat_log_sink & sink_a)
{
	sink_a.begin ();
	if (sink_a.entries () >= config.log_rotation_count)
	{
		sink_a.rotate ();
	}

	if (config.log_headers)
	{
		auto walltime (std::chrono::system_clock::now ());
		sink_a.write_header ("samples", walltime);
	}

	for (auto & it : entries)
	{
		auto key = it.first;
		std::string type = type_to_string (key);
		std::string detail = detail_to_string (key);
		std::string dir = dir_to_string (key);

		for (auto & datapoint : it.second->samples)
		{
			std::time_t time = std::chrono::system_clock::to_time_t (datapoint.timestamp);
			tm local_tm = *localtime (&time);
			sink_a.write_entry (local_tm, type, detail, dir, datapoint.value);
		}
	}
	sink_a.entries ()++;
	sink_a.finalize ();
}

void nano::stat::update (uint32_t key_a, uint64_t value_a)
{
	static file_writer log_count (config.log_counters_filename);
	static file_writer log_sample (config.log_samples_filename);

	auto now (std::chrono::steady_clock::now ());

	std::unique_lock<std::mutex> lock (stat_mutex);
	auto entry (get_entry_impl (key_a, config.interval, config.capacity));

	// Counters
	auto old (entry->counter.value);
	entry->counter.add (value_a);
	entry->count_observers.notify (old, entry->counter.value);

	std::chrono::duration<double, std::milli> duration_l = now - log_last_count_writeout;
	if (config.log_interval_counters > 0 && duration_l.count () > config.log_interval_counters)
	{
		log_counters_impl (log_count);
		log_last_count_writeout = now;
	}

	// Samples
	if (config.sampling_enabled && entry->sample_interval > 0)
	{
		entry->sample_current.add (value_a, false);

		std::chrono::duration<double, std::milli> duration = now - entry->sample_start_time;
		if (duration.count () > entry->sample_interval)
		{
			entry->sample_start_time = now;

			// Make a snapshot of samples for thread safety and to get a stable container
			entry->sample_current.timestamp = std::chrono::system_clock::now ();
			entry->samples.push_back (entry->sample_current);
			entry->sample_current.value = 0;

			if (entry->sample_observers.observers.size () > 0)
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

std::string nano::stat::type_to_string (uint32_t key_a)
{
	auto type = static_cast<stat::type> (key_a >> 16 & 0x000000ff);
	std::string res;
	switch (type)
	{
		case nano::stat::type::block:
			res = "block";
			break;
		case nano::stat::type::bootstrap:
			res = "bootstrap";
			break;
		case nano::stat::type::error:
			res = "error";
			break;
		case nano::stat::type::http_callback:
			res = "http_callback";
			break;
		case nano::stat::type::ledger:
			res = "ledger";
			break;
		case nano::stat::type::udp:
			res = "udp";
			break;
		case nano::stat::type::peering:
			res = "peering";
			break;
		case nano::stat::type::rollback:
			res = "rollback";
			break;
		case nano::stat::type::traffic:
			res = "traffic";
			break;
		case nano::stat::type::traffic_bootstrap:
			res = "traffic_bootstrap";
			break;
		case nano::stat::type::vote:
			res = "vote";
			break;
		case nano::stat::type::message:
			res = "message";
			break;
	}
	return res;
}

std::string nano::stat::detail_to_string (uint32_t key_a)
{
	auto detail = static_cast<stat::detail> (key_a >> 8 & 0x000000ff);
	std::string res;
	switch (detail)
	{
		case nano::stat::detail::all:
			res = "all";
			break;
		case nano::stat::detail::bad_sender:
			res = "bad_sender";
			break;
		case nano::stat::detail::bulk_pull:
			res = "bulk_pull";
			break;
		case nano::stat::detail::bulk_pull_account:
			res = "bulk_pull_account";
			break;
		case nano::stat::detail::bulk_push:
			res = "bulk_push";
			break;
		case nano::stat::detail::change:
			res = "change";
			break;
		case nano::stat::detail::confirm_ack:
			res = "confirm_ack";
			break;
		case nano::stat::detail::node_id_handshake:
			res = "node_id_handshake";
			break;
		case nano::stat::detail::confirm_req:
			res = "confirm_req";
			break;
		case nano::stat::detail::frontier_req:
			res = "frontier_req";
			break;
		case nano::stat::detail::handshake:
			res = "handshake";
			break;
		case nano::stat::detail::http_callback:
			res = "http_callback";
			break;
		case nano::stat::detail::initiate:
			res = "initiate";
			break;
		case nano::stat::detail::initiate_lazy:
			res = "initiate_lazy";
			break;
		case nano::stat::detail::insufficient_work:
			res = "insufficient_work";
			break;
		case nano::stat::detail::keepalive:
			res = "keepalive";
			break;
		case nano::stat::detail::open:
			res = "open";
			break;
		case nano::stat::detail::publish:
			res = "publish";
			break;
		case nano::stat::detail::receive:
			res = "receive";
			break;
		case nano::stat::detail::republish_vote:
			res = "republish_vote";
			break;
		case nano::stat::detail::send:
			res = "send";
			break;
		case nano::stat::detail::state_block:
			res = "state_block";
			break;
		case nano::stat::detail::epoch_block:
			res = "epoch_block";
			break;
		case nano::stat::detail::vote_valid:
			res = "vote_valid";
			break;
		case nano::stat::detail::vote_replay:
			res = "vote_replay";
			break;
		case nano::stat::detail::vote_invalid:
			res = "vote_invalid";
			break;
		case nano::stat::detail::vote_overflow:
			res = "vote_overflow";
			break;
		case nano::stat::detail::blocking:
			res = "blocking";
			break;
		case nano::stat::detail::overflow:
			res = "overflow";
			break;
		case nano::stat::detail::unreachable_host:
			res = "unreachable_host";
			break;
		case nano::stat::detail::invalid_magic:
			res = "invalid_magic";
			break;
		case nano::stat::detail::invalid_network:
			res = "invalid_network";
			break;
		case nano::stat::detail::invalid_header:
			res = "invalid_header";
			break;
		case nano::stat::detail::invalid_message_type:
			res = "invalid_message_type";
			break;
		case nano::stat::detail::invalid_keepalive_message:
			res = "invalid_keepalive_message";
			break;
		case nano::stat::detail::invalid_publish_message:
			res = "invalid_publish_message";
			break;
		case nano::stat::detail::invalid_confirm_req_message:
			res = "invalid_confirm_req_message";
			break;
		case nano::stat::detail::invalid_confirm_ack_message:
			res = "invalid_confirm_ack_message";
			break;
		case nano::stat::detail::invalid_node_id_handshake_message:
			res = "invalid_node_id_handshake_message";
			break;
		case nano::stat::detail::outdated_version:
			res = "outdated_version";
			break;
	}
	return res;
}

std::string nano::stat::dir_to_string (uint32_t key_a)
{
	auto dir = static_cast<stat::dir> (key_a & 0x000000ff);
	std::string res;
	switch (dir)
	{
		case nano::stat::dir::in:
			res = "in";
			break;
		case nano::stat::dir::out:
			res = "out";
			break;
	}
	return res;
}
