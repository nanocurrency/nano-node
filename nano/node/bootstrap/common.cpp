#include <nano/crypto_lib/random_pool.hpp>
#include <nano/lib/enum_util.hpp>
#include <nano/lib/stats_enums.hpp>
#include <nano/node/bootstrap/common.hpp>

namespace nano::bootstrap
{
id_t generate_id ()
{
	return nano::random_pool::generate<id_t> ();
}

nano::stat::detail to_stat_detail (nano::bootstrap::query_type type)
{
	return nano::enum_convert<nano::stat::detail> (type);
}

nano::stat::detail to_stat_detail (nano::bootstrap::strategy strat)
{
	return nano::enum_convert<nano::stat::detail> (strat);
}

std::string_view to_string (nano::bootstrap::strategy strat)
{
	return nano::enum_to_string (strat);
}

nano::stat::type to_inspect_stat_type (nano::bootstrap::strategy source)
{
	switch (source)
	{
		case strategy::invalid:
			return nano::stat::type::bootstrap_inspect_other;
		case strategy::priority:
			return nano::stat::type::bootstrap_inspect_priority;
		case strategy::database:
			return nano::stat::type::bootstrap_inspect_database;
		case strategy::dependency:
			return nano::stat::type::bootstrap_inspect_dependency;
		case strategy::frontier:
			return nano::stat::type::bootstrap_inspect_frontier;
		case strategy::topology:
			return nano::stat::type::bootstrap_inspect_topo;
	}
	debug_assert (false);
	return nano::stat::type::bootstrap_inspect_other;
}
}
