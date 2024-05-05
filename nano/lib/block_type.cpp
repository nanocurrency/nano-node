#include <nano/lib/block_type.hpp>
#include <nano/lib/enum_utils.hpp>

std::string_view nano::to_string (nano::block_type type)
{
	return nano::enum_name (type);
}

void nano::serialize_block_type (nano::stream & stream, const nano::block_type & type)
{
	nano::write (stream, type);
}
