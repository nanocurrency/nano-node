#pragma once

namespace nano
{
/* Holds flags for various cacheable data. For most CLI operations caching is unnecessary
 * (e.g getting the cemented block count) so it can be disabled for performance reasons. */
class generate_cache_flags
{
public:
	bool reps{ true };
	bool cemented_count{ true };
	bool unchecked_count{ true };
	bool account_count{ true };
	bool block_count{ true };
	bool consistency_check{ true };

public:
	static generate_cache_flags all_enabled ()
	{
		return {};
	}

	static generate_cache_flags all_disabled ()
	{
		return {
			.reps = false,
			.cemented_count = false,
			.unchecked_count = false,
			.account_count = false,
			.block_count = false,
			.consistency_check = false,
		};
	}
};
}
