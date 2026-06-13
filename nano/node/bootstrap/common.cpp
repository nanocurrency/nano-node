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

nano::stat::detail to_stat_detail (nano::bootstrap::query_source source)
{
	return nano::enum_convert<nano::stat::detail> (source);
}

nano::stat::detail to_stat_detail (nano::bootstrap::strategy strat)
{
	return nano::enum_convert<nano::stat::detail> (strat);
}
}
