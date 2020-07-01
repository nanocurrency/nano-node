#include <nano/lib/cli.hpp>

#include <boost/format.hpp>

std::vector<std::string> nano::config_overrides (std::vector<config_key_value_pair> const & key_value_pairs_a, bool & error_a)
{
	std::vector<std::string> overrides;
	for (auto pair : key_value_pairs_a)
	{
		// Do not allow escaped quotations
		if (pair.value.find ("\"") != std::string::npos)
		{
			error_a = true;
			break;
		}

		// Make all values strings with escaped quotations so that they are handled correctly by toml parsing
		overrides.push_back ((boost::format ("%1%=\"%2%\"") % pair.key % pair.value).str ());
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
