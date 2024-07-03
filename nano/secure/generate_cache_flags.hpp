#pragma once

namespace nano
{
/* Holds flags for various cacheable data. For most CLI operations caching is unnecessary
 * (e.g getting the cemented block count) so it can be disabled for performance reasons. */
class generate_cache_flags
{
public:
	bool reps = true;
	bool cemented_count = true;
	bool unchecked_count = true;
	bool account_count = true;
	bool block_count = true;

	void enable_all ();

public:
	static generate_cache_flags all_disabled ()
	{
		generate_cache_flags flags;
		flags.reps = false;
		flags.cemented_count = false;
		flags.unchecked_count = false;
		flags.account_count = false;
		flags.block_count = false;
		return flags;
	}
};
}
