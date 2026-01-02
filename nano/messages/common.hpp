#pragma once

#include <nano/lib/assert.hpp>
#include <nano/lib/stream.hpp>

namespace nano::messages
{
struct empty_payload
{
	void serialize (nano::stream &) const
	{
		debug_assert (false);
	}
	void deserialize (nano::stream &)
	{
		debug_assert (false);
	}
	void operator() (nano::object_stream &) const
	{
		debug_assert (false);
	}
};
}