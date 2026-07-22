#pragma once

#include <nano/node/bootstrap/bootstrap_config.hpp>
#include <nano/node/fwd.hpp>

#include <cstddef>
#include <deque>

namespace nano::bootstrap
{
/**
 * Adaptive fast-forward policy for the topology spearhead.
 *
 * The history starts filled with pages that needed fetching to make fast-forwarding conservative at startup.
 * Each recently prechecked page which still needed fetching adds one page to the evidence required before
 * fast-forwarding. Once the threshold is reached, the policy stays engaged and keeps fast-forwarding on every
 * redundant page until a page which needs fetching disengages it.
 */
class topo_skip_policy
{
public:
	explicit topo_skip_policy (nano::topo_scan_config const &);

	// Observe one spearhead page. Returns true when the caller should attempt a fast-forward.
	bool observe (bool needs_fetch);

	void reset ();

	std::size_t threshold () const;
	nano::container_info container_info () const;

private:
	void add_history (bool needs_fetch);
	std::size_t next_threshold () const;

	nano::topo_scan_config const & config;

	std::deque<bool> history;

	std::size_t recent_need_fetch_pages_m{ 0 };
	std::size_t redundant_pages_m{ 0 };
	std::size_t threshold_m{ 0 };
	bool engaged_m{ false };
};
}
