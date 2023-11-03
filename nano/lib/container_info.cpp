#include <nano/lib/container_info.hpp>

nano::container_info_composite::container_info_composite (std::string const & name) :
	name (name)
{
}

bool nano::container_info_composite::is_composite () const
{
	return true;
}

void nano::container_info_composite::add_component (std::unique_ptr<container_info_component> child)
{
	children.push_back (std::move (child));
}

std::vector<std::unique_ptr<nano::container_info_component>> const & nano::container_info_composite::get_children () const
{
	return children;
}

std::string const & nano::container_info_composite::get_name () const
{
	return name;
}

nano::container_info_leaf::container_info_leaf (const container_info & info) :
	info (info)
{
}

bool nano::container_info_leaf::is_composite () const
{
	return false;
}

nano::container_info const & nano::container_info_leaf::get_info () const
{
	return info;
}