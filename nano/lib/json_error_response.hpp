#pragma once

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

namespace nano
{
inline void json_error_response (std::function<void (std::string const &)> response_a, std::string const & message_a)
{
	boost::property_tree::ptree response_l;
	response_l.put ("error", message_a);
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, response_l);
	response_a (ostream.str ());
}
}
