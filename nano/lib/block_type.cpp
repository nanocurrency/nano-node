#include <celerix/lib/block_type.hpp>
#include <celerix/lib/enum_util.hpp>

std::string_view celerix::to_string (celerix::block_type type)
{
	return celerix::enum_util::name (type);
}
