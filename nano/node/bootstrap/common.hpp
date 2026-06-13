#pragma once

#include <nano/lib/stats_enums.hpp>

#include <cstdint>

namespace nano::bootstrap
{
using id_t = uint64_t;

id_t generate_id ();

enum class query_type
{
	invalid = 0,
	blocks_by_hash,
	blocks_by_account,
	account_info_by_hash,
	frontiers,
};

enum class query_source
{
	invalid = 0,
	priority,
	database,
	dependencies,
	frontiers,
};

enum class verify_result
{
	ok,
	nothing_new,
	invalid,
};

enum class strategy
{
	priority,
	database,
	dependency,
	frontier,
};

nano::stat::detail to_stat_detail (nano::bootstrap::query_type);
nano::stat::detail to_stat_detail (nano::bootstrap::query_source);
nano::stat::detail to_stat_detail (nano::bootstrap::strategy);
}
