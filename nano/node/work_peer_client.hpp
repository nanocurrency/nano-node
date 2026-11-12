#pragma once

#include <nano/lib/fwd.hpp>
#include <nano/node/work_request.hpp>

#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <string_view>

namespace asio = boost::asio;

namespace nano
{
// How one exchange with a work peer ended
enum class work_peer_status
{
	success, // The peer answered with work that reaches the difficulty
	cancelled, // The exchange was abandoned by this node
	timeout, // The peer did not answer before the deadline
	unreachable, // The peer could not be resolved or connected to
	io_error, // The connection broke
	http_error, // The peer answered with an HTTP status other than 200
	malformed, // The answer could not be read as a work response
	refused, // The peer answered with an error
	insufficient, // The peer answered with work below the difficulty
};

struct work_peer_response
{
	nano::work_peer_status status{ nano::work_peer_status::io_error };
	uint64_t work{ 0 }; // Only meaningful on success
	bool request_sent{ false }; // Whether the peer may have started working on the request
};

/**
 * Speaks the work RPC to one work peer per call, each call is a single HTTP exchange over its own connection.
 * The calls run on the executor of the coroutine awaiting them, never throw and end early when that coroutine is cancelled
 */
class work_peer_client final
{
public:
	// Time limits of an exchange that do not depend on the request
	struct timeouts
	{
		std::chrono::milliseconds connect{ 5000 }; // Resolving the peer's name and connecting to it
		std::chrono::milliseconds cancel{ 2000 }; // A whole `work_cancel` exchange
	};

public:
	work_peer_client (nano::work_thresholds const &, timeouts const &, nano::logger &);

	// Sends `work_generate` and waits for the peer's answer until the deadline, checking the work it returns
	asio::awaitable<nano::work_peer_response> generate (nano::work_peer, nano::work_request, std::chrono::steady_clock::time_point deadline) const;

	// Sends `work_cancel` for the root, without reporting whether the peer took it
	asio::awaitable<void> cancel (nano::work_peer, nano::root) const;

private: // Dependencies
	nano::work_thresholds const & thresholds;
	nano::logger & logger;

private:
	// Reads a peer's answer to `work_generate` and checks the work against the request
	nano::work_peer_response parse (unsigned http_status, std::string const & body, nano::work_request const &) const;

	timeouts const config;
};

std::string_view to_string (nano::work_peer_status);
nano::stat::detail to_stat_detail (nano::work_peer_status);
}
