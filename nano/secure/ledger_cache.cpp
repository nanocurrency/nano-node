#include <nano/secure/ledger_cache.hpp>

nano::ledger_cache::ledger_cache (nano::store::rep_weight & confirmed, nano::store::rocksdb::unconfirmed_rep_weight & unconfirmed, nano::uint128_t min_rep_weight_a) :
	rep_weights{ confirmed, unconfirmed, min_rep_weight_a }
{
}
