#pragma once

#include <iostream>
#include <string>
#include <vector>

namespace boost::program_options
{
class variables_map;
}

namespace nano
{
class config_key_value_pair
{
public:
	std::string key;
	std::string value;
};

/** Renders `key=value` pairs from the command line as config override entries */
std::vector<std::string> config_overrides (std::vector<config_key_value_pair> const & key_value_pairs_a);
/** Config override entries passed through the command line option `option_name`; empty when the option is absent */
std::vector<std::string> config_overrides (boost::program_options::variables_map const & vm, std::string const & option_name = "config");

std::istream & operator>> (std::istream & is, nano::config_key_value_pair & into);
}
