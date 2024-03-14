#include <nano/lib/block_type.hpp>

#include <magic_enum.hpp>

std::string_view nano::to_string (nano::block_type type)
{
	return magic_enum::enum_name (type);
}

void nano::serialize_block_type (nano::stream & stream, const nano::block_type & type)
{
	nano::write (stream, type);
}
