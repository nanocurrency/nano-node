#include <celerix/secure/ledger_cache.hpp>

celerix::ledger_cache::ledger_cache (celerix::store::rep_weight & rep_weight_store_a, celerix::uint128_t min_rep_weight_a) :
	rep_weights{ rep_weight_store_a, min_rep_weight_a }
{
}
