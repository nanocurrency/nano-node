#pragma once

#include <boost/log/sources/logger.hpp>
#include <boost/log/trivial.hpp>
#include <chrono>

namespace nano
{
// A wrapper around a boost logger object to allow minimum
// time spaced output to prevent logging happening too quickly.
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
	 * @param log_items Output to the log file if the last write was over min_log_delta time ago.
	 * @return true if the log was successful
	 */
	template <typename... LogItems>
	bool try_log (LogItems &&... log_items)
	{
		auto error (true);
		auto time_now = std::chrono::steady_clock::now ();
		if (((time_now - last_log_time) > min_log_delta) || last_log_time == std::chrono::steady_clock::time_point{})
		{
			output (std::forward<LogItems> (log_items)...);
			last_log_time = time_now;
			error = false;
		}
		return error;
	}

	std::chrono::milliseconds min_log_delta;

private:
	std::chrono::steady_clock::time_point last_log_time;
	boost::log::sources::logger_mt boost_logger_mt;
};
}
