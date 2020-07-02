#include <nano/lib/cli.hpp>

#include <boost/format.hpp>

std::vector<std::string> nano::config_overrides (std::vector<config_key_value_pair> const & key_value_pairs_a)
{
	std::vector<std::string> overrides;
	auto format (boost::format ("%1%=%2%"));
	auto format_add_escaped_quotes (boost::format ("%1%=\"%2%\""));
	for (auto pair : key_value_pairs_a)
	{
		// Should be equal number of escaped quotes if any are found.
		auto already_escaped = pair.value.find ('\"') != std::string::npos;
		// Make all values strings with escaped quotations so that they are handled correctly by toml parsing
		overrides.push_back (((!already_escaped ? format_add_escaped_quotes : format) % pair.key % pair.value).str ());
	}
	return overrides;
}

std::istream & nano::operator>> (std::istream & is, nano::config_key_value_pair & into)
{
	char ch;
	while (is >> ch && ch != '=')
	{
		into.key += ch;
	}
	return is >> into.value;
}
