#pragma once

#include <nano/lib/object_stream.hpp>

#include <ostream>
#include <sstream>

#include <fmt/ostream.h>

namespace nano
{
template <class Streamable>
struct object_stream_formatter
{
	nano::object_stream_config const & config;
	Streamable const & value;

	explicit object_stream_formatter (Streamable const & value, nano::object_stream_config const & config) :
		config{ config },
		value{ value }
	{
	}

	friend std::ostream & operator<< (std::ostream & os, object_stream_formatter<Streamable> const & self)
	{
		nano::root_object_stream obs{ os, self.config };
		obs.write (self.value);
		return os;
	}

	// Needed for fmt formatting, uses the ostream operator under the hood
	friend auto format_as (object_stream_formatter<Streamable> const & val)
	{
		return fmt::streamed (val);
	}
};

template <class Streamable>
auto streamed (Streamable const & value)
{
	return object_stream_formatter{ value, nano::object_stream_config::default_config () };
}

template <class Streamable>
auto streamed_as_json (Streamable const & value)
{
	return object_stream_formatter{ value, nano::object_stream_config::json_config () };
}

/**
 * Wraps {name,value} args and provides `<<(std::ostream &, ...)` and fmt format operator that writes the arguments to the stream in a lazy manner.
 */
template <class... Args>
struct object_stream_args_formatter
{
	nano::object_stream_config const & config;
	std::tuple<Args...> args;

	explicit object_stream_args_formatter (nano::object_stream_config const & config, Args &&... args) :
		config{ config },
		args{ std::forward<Args> (args)... }
	{
	}

	friend std::ostream & operator<< (std::ostream & os, object_stream_args_formatter<Args...> const & self)
	{
		nano::object_stream obs{ os, self.config };
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
auto streamed_args (nano::object_stream_config const & config, Args &&... args)
{
	return object_stream_args_formatter<Args...>{ config, std::forward<Args> (args)... };
}
}

/*
 * Adapter that allows for printing using '<<' operator for all classes that implement object streaming
 */
namespace nano::object_stream_adapters
{
template <nano::object_or_array_streamable Value>
std::ostream & operator<< (std::ostream & os, Value const & value)
{
	return os << nano::streamed (value);
}

template <nano::object_or_array_streamable Value>
std::string to_string (Value const & value)
{
	std::stringstream ss;
	ss << nano::streamed (value);
	return ss.str ();
}

template <nano::object_or_array_streamable Value>
std::string to_json (Value const & value)
{
	std::stringstream ss;
	ss << nano::streamed_as_json (value);
	return ss.str ();
}
}

/*
 * Adapter that allows for printing using fmt library for all classes that implement object streaming
 */
template <nano::object_or_array_streamable Streamable>
struct fmt::formatter<Streamable> : fmt::ostream_formatter
{
	auto format (Streamable const & value, format_context & ctx)
	{
		return fmt::ostream_formatter::format (nano::streamed (value), ctx);
	}
};