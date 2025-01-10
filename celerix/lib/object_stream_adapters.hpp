#pragma once

#include <celerix/lib/object_stream.hpp>
#include <celerix/lib/utility.hpp>

#include <ostream>
#include <sstream>

#include <fmt/ostream.h>

namespace celerix
{
template <class Streamable, class Writer>
struct object_stream_formatter
{
	celerix::object_stream_config const & config;
	Streamable const & value;
	Writer writer;

	explicit object_stream_formatter (Streamable const & value, Writer writer, celerix::object_stream_config const & config) :
		config{ config },
		value{ value },
		writer{ writer }
	{
	}

	friend std::ostream & operator<< (std::ostream & os, object_stream_formatter<Streamable, Writer> const & self)
	{
		celerix::root_object_stream obs{ os, self.config };
		self.writer (self.value, obs);
		return os;
	}

	// Needed for fmt formatting, uses the ostream operator under the hood
	friend auto format_as (object_stream_formatter<Streamable, Writer> const & self)
	{
		return fmt::streamed (self);
	}
};

enum class streamed_format
{
	basic,
	json
};

inline celerix::object_stream_config const & to_object_stream_config (streamed_format format)
{
	switch (format)
	{
		case streamed_format::basic:
			return celerix::object_stream_config::default_config ();
		case streamed_format::json:
			return celerix::object_stream_config::json_config ();
		default:
			debug_assert (false);
			return celerix::object_stream_config::default_config ();
	}
}

template <class Streamable>
auto streamed (Streamable const & value, streamed_format format = streamed_format::basic)
{
	return object_stream_formatter{ value, [] (auto const & value, celerix::root_object_stream & obs) { obs.write (value); }, to_object_stream_config (format) };
}

template <class StreamableRange>
auto streamed_range (StreamableRange const & value, streamed_format format = streamed_format::basic)
{
	return object_stream_formatter{ value, [] (auto const & value, celerix::root_object_stream & obs) { obs.write_range (value); }, to_object_stream_config (format) };
}

/**
 * Wraps {name,value} args and provides `<<(std::ostream &, ...)` and fmt format operator that writes the arguments to the stream in a lazy manner.
 */
template <class... Args>
struct object_stream_args_formatter
{
	celerix::object_stream_config const & config;
	std::tuple<Args...> args;

	explicit object_stream_args_formatter (celerix::object_stream_config const & config, Args &&... args) :
		config{ config },
		args{ std::forward<Args> (args)... }
	{
	}

	friend std::ostream & operator<< (std::ostream & os, object_stream_args_formatter<Args...> const & self)
	{
		celerix::object_stream obs{ os, self.config };
		std::apply ([&obs] (auto &&... args) {
			((obs.write (args.name, args.value)), ...);
		},
		self.args);
		return os;
	}

	// Needed for fmt formatting, uses the ostream operator under the hood
	friend auto format_as (object_stream_args_formatter<Args...> const & val)
	{
		return fmt::streamed (val);
	}
};

template <class... Args>
auto streamed_args (celerix::object_stream_config const & config, Args &&... args)
{
	return object_stream_args_formatter<Args...>{ config, std::forward<Args> (args)... };
}
}

/*
 * Adapter that allows for printing using '<<' operator for all classes that implement object streaming
 */
namespace celerix::object_stream_adapters
{
template <celerix::object_or_array_streamable Value>
std::ostream & operator<< (std::ostream & os, Value const & value)
{
	return os << celerix::streamed (value);
}

template <celerix::object_or_array_streamable Value>
std::string to_string (Value const & value)
{
	std::stringstream ss;
	ss << celerix::streamed (value);
	return ss.str ();
}

template <celerix::object_or_array_streamable Value>
std::string to_json (Value const & value)
{
	std::stringstream ss;
	ss << celerix::streamed (value, celerix::streamed_format::json);
	return ss.str ();
}
}

/*
 * Adapter that allows for printing using fmt library for all classes that implement object streaming
 */
template <celerix::object_or_array_streamable Streamable>
struct fmt::formatter<Streamable> : fmt::ostream_formatter
{
	auto format (Streamable const & value, format_context & ctx)
	{
		return fmt::ostream_formatter::format (celerix::streamed (value), ctx);
	}
};