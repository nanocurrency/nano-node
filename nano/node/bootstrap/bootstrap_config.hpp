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
	std::size_t consideration_count{ 4 }; // Not read by the account sets; kept as a config key
	std::size_t priorities_max{ 256 * 1024 }; // Cap on prioritized accounts; the lowest priorities are trimmed past it
	std::size_t blocking_max{ 256 * 1024 }; // Cap on accounts blocked on a missing dependency; the oldest are trimmed past it
	std::chrono::milliseconds cooldown{ 1000 * 3 }; // Minimum time before a prioritized account is requested again
	std::chrono::seconds blocking_decay{ 15min }; // Blocked accounts older than this are dropped from the blocking set
};

class frontier_scan_config final
{
public:
	nano::error deserialize (nano::tomlconfig & toml);
	nano::error serialize (nano::tomlconfig & toml) const;

public:
	unsigned head_parallelism{ 128 }; // Number of equal account ranges scanned in parallel, one head each
	unsigned consideration_count{ 4 }; // Requests a head sends right away and responses it collects before it advances
	std::size_t candidates{ 1000 }; // Cap on candidate accounts kept per head (the smallest)
	std::chrono::milliseconds cooldown{ 1000 * 5 }; // Time before a head that already sent consideration_count requests is requested again
	std::size_t max_pending{ 16 }; // Queued frontier batches before new requests pause; responses past four times this are dropped
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
	std::size_t repair_rate_limit{ 20 }; // Topology pages scanned per second across all repair heads; every page re-verifies up to max_entries keys against the ledger, so this bounds the sweep's disk load
	std::size_t candidates{ nano::bootstrap_server::max_topo_entries - 1 }; // Cap on new aggregated entries kept (the smallest) per advance; topo_index responses include the cursor
	std::size_t redundant_skip_threshold{ 100 }; // Consecutive fully-redundant spearhead pages before fast-forwarding
	std::size_t redundant_skip_history_size{ 200 }; // Recent spearhead pages remembered; each page that needed fetching raises the next skip threshold by one
	uint64_t redundant_skip_stride{ 10'000 }; // Topo-height stride to fast-forward after redundant_skip_threshold pages
	std::chrono::milliseconds cooldown{ 1000 * 5 }; // Per-head cooldown between requests
	std::size_t max_buffered{ 1000 * 40 }; // Scan back-pressure: pause all discovery while the buffer holds this many entries
	std::size_t fetch_batch{ nano::bootstrap_server::max_random_blocks }; // Hashes per blocks_random request (<= max_random_hashes); a peer returns at most max_random_blocks, leaving the rest for a retry
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
	bool enable{ true }; // Disabling stops the node from syncing
	bool enable_priorities{ true }; // Run the priorities strategy
	bool enable_database_scan{ false }; // Run the database scan strategy
	bool enable_dependency_walker{ true }; // Run the dependency walker strategy
	bool enable_frontier_scan{ true }; // Run the frontier scan strategy
	bool enable_topo_scan{ true }; // Run the topology scan strategy

	// Maximum number of un-responded requests per channel, should be lower or equal to bootstrap server max queue size
	std::size_t channel_limit{ 16 };
	std::size_t max_requests{ 1000 }; // Cap on in-flight requests across all channels
	std::chrono::milliseconds request_timeout{ 1000 * 15 }; // Time a peer has to respond once a request is sent; four times this while still queued for sending

	// Global rate_limit is retained for back-compat config keys but is no longer applied; rate limiting is per-strategy
	std::size_t rate_limit{ 500 };
	std::size_t priority_rate_limit{ 500 }; // Priorities requests per second
	std::size_t database_rate_limit{ 250 }; // Database scan requests per second
	std::size_t dependency_rate_limit{ 500 }; // Dependency walker requests per second
	std::size_t frontier_rate_limit{ 15 }; // Frontier scan requests per second
	std::size_t topo_rate_limit{ 500 }; // Topology scan and random block fetch requests per second

	std::size_t database_warmup_ratio{ 10 }; // Database limiter tokens one database request costs while the scan warms up and bootstrap is throttled
	std::size_t max_pull_count{ nano::bootstrap_server::max_blocks }; // Cap on blocks asked for in one priorities request
	std::size_t throttle_coefficient{ 8000 }; // Throttle sample window is this times the log of the account count, at least 16
	std::chrono::milliseconds throttle_wait{ 100 }; // Upper bound on the backoff interval while waiting for a condition, doubling from 5 ms
	std::size_t block_processor_threshold{ 1000 }; // A strategy waits while its block processor queue holds this many blocks
	unsigned optimistic_request_percentage{ 75 }; // Share of priorities requests starting from the account frontier rather than the confirmed frontier

	account_sets_config account_sets;
	frontier_scan_config frontier_scan;
	topo_scan_config topo_scan;
};
}
