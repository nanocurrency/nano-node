#include <nano/node/work_request.hpp>

std::string nano::work_peer::to_string () const
{
	return address + ":" + std::to_string (port);
}

std::optional<uint64_t> nano::work_result::value () const
{
	if (status == nano::work_status::success)
	{
		return work;
	}

	return std::nullopt;
}
