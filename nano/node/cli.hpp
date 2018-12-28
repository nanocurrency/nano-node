#pragma once

#include <boost/program_options.hpp>
#include <nano/lib/errors.hpp>

namespace nano
{
/** Command line related error codes */
enum class error_cli
{
	generic = 1,
	parse_error = 2,
	invalid_arguments = 3,
	unknown_command = 4
};

void add_node_options (boost::program_options::options_description &);
std::error_code handle_node_options (boost::program_options::variables_map &);
}

REGISTER_ERROR_CODES (nano, error_cli)
