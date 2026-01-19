#include <nano/lib/enum_util.hpp>
#include <nano/store/common.hpp>

std::string_view nano::store::to_string (nano::store::open_mode mode)
{
	return nano::enum_to_string (mode);
}