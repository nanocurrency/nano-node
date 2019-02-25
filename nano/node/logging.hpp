#pragma once

#include <boost/filesystem.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/log/trivial.hpp>
#include <cstdint>
#include <nano/lib/errors.hpp>
#include <nano/lib/jsonconfig.hpp>

#define FATAL_LOG_PREFIX "FATAL ERROR: "

namespace nano
{
// A wrapper around a boost logger object to allow
// minimum time spaced output to prevent logging happening
// too quickly.
class logger_mt
{
private:
	void add_to_stream (boost::log::record_ostream & stream)
	{
	}

	template <typename LogItem, typename... LogItems>
	void add_to_stream (boost::log::record_ostream & stream, const LogItem & first_log_item, LogItems &&... remainder_log_items)
	{
		stream << first_log_item;
		add_to_stream (stream, remainder_log_items...);
	}

	template <typename... LogItems>
	void output (LogItems &&... log_items)
	{
		boost::log::record rec = boost_logger_mt.open_record ();
		if (rec)
		{
			boost::log::record_ostream strm (rec);
			add_to_stream (strm, std::forward<LogItems> (log_items)...);
			strm.flush ();
			boost_logger_mt.push_record (std::move (rec));
		}
	}

public:
	/**
	 * @param min_log_delta_a The minimum time between successive output
	 */
	explicit logger_mt (std::chrono::milliseconds const & min_log_delta_a) :
	min_log_delta (min_log_delta_a)
	{
	}

	/*
	 * @param log_items A collection of objects with overloaded operator<< to be output to the log file
	 */
	template <typename... LogItems>
	void always_log (LogItems &&... log_items)
	{
		output (std::forward<LogItems> (log_items)...);
	}

	/*
	 * @param log_items A collection of objects with overloaded operator<< to be output to the log file
	 *					if the last time an item was logged was over min_log_delta time ago.
	 * @return true if the log was successful
	 */
	template <typename... LogItems>
	bool try_log (LogItems &&... log_items)
	{
		auto logged (false);
		auto time_now = std::chrono::steady_clock::now ();
		if ((time_now - last_log_time) > min_log_delta)
		{
			output (std::forward<LogItems> (log_items)...);
			last_log_time = time_now;
			logged = true;
		}
		return logged;
	}

private:
	std::chrono::milliseconds const & min_log_delta;
	std::chrono::steady_clock::time_point last_log_time;
	boost::log::sources::logger_mt boost_logger_mt;
};

class logging
{
public:
	nano::error serialize_json (nano::jsonconfig &) const;
	nano::error deserialize_json (bool &, nano::jsonconfig &);
	bool upgrade_json (unsigned, nano::jsonconfig &);
	bool ledger_logging () const;
	bool ledger_duplicate_logging () const;
	bool vote_logging () const;
	bool network_logging () const;
	bool network_message_logging () const;
	bool network_publish_logging () const;
	bool network_packet_logging () const;
	bool network_keepalive_logging () const;
	bool network_node_id_handshake_logging () const;
	bool node_lifetime_tracing () const;
	bool insufficient_work_logging () const;
	bool upnp_details_logging () const;
	bool timing_logging () const;
	bool log_rpc () const;
	bool log_ipc () const;
	bool bulk_pull_logging () const;
	bool callback_logging () const;
	bool work_generation_time () const;
	bool log_to_cerr () const;
	void init (boost::filesystem::path const &);

	bool ledger_logging_value{ false };
	bool ledger_duplicate_logging_value{ false };
	bool vote_logging_value{ false };
	bool network_logging_value{ true };
	bool network_message_logging_value{ false };
	bool network_publish_logging_value{ false };
	bool network_packet_logging_value{ false };
	bool network_keepalive_logging_value{ false };
	bool network_node_id_handshake_logging_value{ false };
	bool node_lifetime_tracing_value{ false };
	bool insufficient_work_logging_value{ true };
	bool log_rpc_value{ true };
	bool log_ipc_value{ true };
	bool bulk_pull_logging_value{ false };
	bool work_generation_time_value{ true };
	bool upnp_details_logging_value{ false };
	bool timing_logging_value{ false };
	bool log_to_cerr_value{ false };
	bool flush{ true };
	uintmax_t max_size{ 128 * 1024 * 1024 };
	uintmax_t rotation_size{ 4 * 1024 * 1024 };
	std::chrono::milliseconds min_time_between_log_output{ 5 };
	nano::logger_mt logger{ min_time_between_log_output };
	int json_version () const
	{
		return 7;
	}
};
}
