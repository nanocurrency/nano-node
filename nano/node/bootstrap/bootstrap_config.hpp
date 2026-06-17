#pragma once

#include <nano/lib/errors.hpp>
#include <nano/lib/timer.hpp>
#include <nano/node/bootstrap/bootstrap_server.hpp>

#include <cstdint>

using namespace std::chrono_literals;

namespace nano
{
class tomlconfig;

class account_sets_config final
{
public:
	nano::error deserialize (nano::tomlconfig & toml);
	nano::error serialize (nano::tomlconfig & toml) const;

public:
	std::size_t consideration_count{ 4 };
	std::size_t priorities_max{ 256 * 1024 };
	std::size_t blocking_max{ 256 * 1024 };
	std::chrono::milliseconds cooldown{ 1000 * 3 };
	std::chrono::seconds blocking_decay{ 15min };
};

class frontier_scan_config final
{
public:
	nano::error deserialize (nano::tomlconfig & toml);
	nano::error serialize (nano::tomlconfig & toml) const;

public:
	unsigned head_parallelism{ 128 };
	unsigned consideration_count{ 4 };
	std::size_t candidates{ 1000 };
	std::chrono::milliseconds cooldown{ 1000 * 5 };
	std::size_t max_pending{ 16 };
};

class topo_scan_config final
{
public:
	nano::error deserialize (nano::tomlconfig & toml);
	nano::error serialize (nano::tomlconfig & toml) const;

public:
	std::size_t repair_band_height{ 2'000'000 }; // Topo-height each repair head sweeps; the repair head count scales as frontier_height / this
	unsigned min_repair_heads{ 2 }; // Floor on repair heads, so a small ledger still has some repair parallelism
	unsigned max_repair_heads{ 6 }; // Cap on repair heads; beyond it bands grow rather than the head count
	unsigned consideration_count{ 3 }; // Spearhead: distinct peers sampled before the frontier advances
	unsigned repair_consideration{ 1 }; // Repair: distinct peers sampled before a band cursor advances
	std::size_t candidates{ nano::bootstrap_server::max_topo_entries - 1 }; // Cap on new aggregated entries kept (the smallest) per advance; topo_index responses include the cursor
	std::size_t redundant_skip_threshold{ 3 }; // Consecutive fully-redundant spearhead pages before fast-forwarding
	uint64_t redundant_skip_stride{ 10'000 }; // Topo-height stride to fast-forward after redundant_skip_threshold pages
	std::chrono::milliseconds cooldown{ 1000 * 5 }; // Per-head cooldown between requests
	std::size_t max_buffered{ 1000 * 40 }; // Scan back-pressure: pause all discovery while the buffer holds this many entries
	std::size_t fetch_batch{ 128 }; // Hashes per blocks_random request (<= 128)
	unsigned max_fetch_attempts{ 10 }; // Demote an entry to a tolerated gap after this many failed fetch rounds
	std::chrono::milliseconds fetch_cooldown{ 1000 * 2 }; // Minimum time before retrying a requested entry
	std::size_t max_gaps{ 1000 * 50 }; // Spearhead back-pressure: pause forward discovery while this many submitted blocks are stuck as gaps
	std::chrono::milliseconds gap_ttl{ 10min }; // Evict a stuck gap after this long so a permanently-missing dependency cannot wedge the spearhead
};

class bootstrap_config final
{
public:
	nano::error deserialize (nano::tomlconfig & toml);
	nano::error serialize (nano::tomlconfig & toml) const;

public:
	bool enable{ true };
	bool enable_priorities{ true };
	bool enable_database_scan{ false };
	bool enable_dependency_walker{ true };
	bool enable_frontier_scan{ true };
	bool enable_topo_scan{ true };

	// Maximum number of un-responded requests per channel, should be lower or equal to bootstrap server max queue size
	std::size_t channel_limit{ 16 };
	std::size_t max_requests{ 1000 };
	std::chrono::milliseconds request_timeout{ 1000 * 15 };

	// Global rate_limit is retained for back-compat config keys but is no longer applied; rate limiting is per-strategy
	std::size_t rate_limit{ 500 };
	std::size_t priority_rate_limit{ 500 };
	std::size_t database_rate_limit{ 250 };
	std::size_t dependency_rate_limit{ 500 };
	std::size_t frontier_rate_limit{ 15 };
	std::size_t topo_rate_limit{ 500 };

	std::size_t database_warmup_ratio{ 10 };
	std::size_t max_pull_count{ nano::bootstrap_server::max_blocks };
	std::size_t throttle_coefficient{ 8000 };
	std::chrono::milliseconds throttle_wait{ 100 };
	std::size_t block_processor_threshold{ 1000 };
	unsigned optimistic_request_percentage{ 75 };

	account_sets_config account_sets;
	frontier_scan_config frontier_scan;
	topo_scan_config topo_scan;
};
}
