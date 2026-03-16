#include <nano/lib/enum_flags_templ.hpp>
#include <nano/lib/enum_util.hpp>
#include <nano/lib/node_capabilities.hpp>

std::string_view nano::to_string (nano::node_capabilities value)
{
	return nano::enum_to_string (value);
}

template std::ostream & nano::operator<< <nano::node_capabilities> (std::ostream &, nano::node_capabilities_flags const &);
template std::string nano::to_string<nano::node_capabilities> (nano::node_capabilities_flags const &);
