#include <celerix/lib/object_stream.hpp>

celerix::object_stream_config const & celerix::object_stream_config::default_config ()
{
	static object_stream_config const config{};
	return config;
}

celerix::object_stream_config const & celerix::object_stream_config::json_config ()
{
	static object_stream_config const config{
		.field_name_begin = "\"",
		.field_name_end = "\"",
		.field_assignment = ":",
		.field_separator = ",",
		.object_begin = "{",
		.object_end = "}",
		.array_begin = "[",
		.array_end = "]",
		.array_element_begin = "",
		.array_element_end = "",
		.array_element_separator = ",",
		.indent = "",
		.newline = "",
		.precision = 4,
	};
	return config;
}