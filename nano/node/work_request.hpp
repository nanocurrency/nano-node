#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/work_version.hpp>

#include <chrono>
#include <compare>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>

namespace nano
{
// A work server reachable over HTTP
struct work_peer
{
	std::string address; // Host name or IP address
	uint16_t port{ 0 };

	auto operator<=> (work_peer const &) const = default;

	// The peer as "address:port"
	std::string to_string () const;
};

// The configured lists of work peers a request can go to
enum class work_peer_group
{
	primary, // The `work_peers` of the node config
	secondary, // The `secondary_work_peers` of the node config
};

// What to generate work for, and who may generate it
struct work_request
{
	nano::work_version version{ nano::work_version::work_1 };
	nano::root root{ 0 };
	uint64_t difficulty{ 0 }; // Threshold the work has to reach
	std::optional<nano::account> account; // Passed on to work peers, which may use it for accounting
	std::optional<nano::work_peer_group> peers{ nano::work_peer_group::primary }; // Work peers that may be asked, empty keeps the request on this node
};

enum class work_status
{
	success, // Work was generated
	cancelled, // The request was cancelled, or the node is stopping
	failed, // Nothing that could generate the work delivered it
};

// How a request ended
struct work_result
{
	nano::work_status status{ nano::work_status::failed };
	uint64_t work{ 0 }; // Only meaningful on success

	// The work on success, empty otherwise
	std::optional<uint64_t> value () const;
};

using work_callback = std::function<void (nano::work_result const &)>;

// A finished request as reported to observers
struct work_summary
{
	nano::work_request request;
	nano::work_result result;
	std::string source; // "local" or the work peer that delivered, empty unless the request succeeded
	std::chrono::milliseconds duration{ 0 }; // Time from the request to its result
	std::deque<std::string> bad_peers; // Work peers that failed while the request was running
};
}
