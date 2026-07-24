#include <nano/lib/enum_util.hpp>
#include <nano/store/tables.hpp>

std::string_view nano::store::to_string (nano::store::table table)
{
	return nano::enum_to_string (table);
}
