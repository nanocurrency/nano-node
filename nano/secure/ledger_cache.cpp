#include <nano/secure/ledger_cache.hpp>

nano::ledger_cache::ledger_cache (nano::store::rep_weight & rep_weight_store_a, nano::uint128_t min_rep_weight_a) :
	rep_weights{ rep_weight_store_a, min_rep_weight_a }
{
}
