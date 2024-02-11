#pragma once

#include <nano/lib/stats.hpp>

#include <boost/format.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

namespace nano
{
/** JSON sink. The resulting JSON object is provided as both a property_tree::ptree (to_object) and a string (to_string) */
class stat_json_writer : public nano::stat_log_sink
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

	std::string to_string () override
	{
		boost::property_tree::write_json (sstr, tree);
		return sstr.str ();
	}

	// WARNING: This method moves the ptree out of the object, leaving it in an undefined state
	boost::property_tree::ptree && to_ptree ()
	{
		return std::move (tree);
	}

private:
	std::ostringstream sstr;
};

/** File sink with rotation support. This writes one counter per line and does not include histogram values. */
class stat_file_writer : public nano::stat_log_sink
{
public:
	std::ofstream log;
	std::string filename;

	explicit stat_file_writer (std::string const & filename) :
		filename (filename)
	{
		log.open (filename.c_str (), std::ofstream::out);
	}

	~stat_file_writer () override
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