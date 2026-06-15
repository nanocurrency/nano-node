#pragma once

#include <nano/lib/stats_enums.hpp>

#include <cstdint>
#include <string_view>

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

enum class verify_result
{
	ok,
	nothing_new,
	invalid,
};

enum class strategy
{
	invalid = 0,
	priority,
	database,
	dependency,
	frontier,
};

nano::stat::detail to_stat_detail (nano::bootstrap::query_type);
nano::stat::detail to_stat_detail (nano::bootstrap::strategy);

std::string_view to_string (nano::bootstrap::strategy);

nano::stat::type to_inspect_stat_type (strategy);
}
