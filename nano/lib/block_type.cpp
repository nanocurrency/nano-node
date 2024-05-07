#include <nano/lib/block_type.hpp>
#include <nano/lib/enum_util.hpp>

std::string_view nano::to_string (nano::block_type type)
{
	return nano::enum_util::name (type);
}

void nano::serialize_block_type (nano::stream & stream, const nano::block_type & type)
{
	nano::write (stream, type);
}
