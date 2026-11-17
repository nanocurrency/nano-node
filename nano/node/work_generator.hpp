#pragma once

#include <nano/lib/async.hpp>
#include <nano/lib/fwd.hpp>
#include <nano/lib/locks.hpp>
#include <nano/lib/numbers_templ.hpp>
#include <nano/lib/observer_set.hpp>
#include <nano/lib/work_peer_client.hpp>
#include <nano/lib/work_request.hpp>
#include <nano/node/fwd.hpp>
#include <nano/node/work_generator_index.hpp>

#include <boost/asio.hpp>

#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

namespace asio = boost::asio;

namespace nano
{
class work_generator_config final
{
public:
	explicit work_generator_config (nano::network_constants const &);

	nano::error deserialize (nano::tomlconfig &);
	nano::error serialize (nano::tomlconfig &) const;

public:
	nano::work_local_mode local_generation{ nano::work_local_mode::fallback };
	std::chrono::milliseconds local_delay{ 5000 }; // In `delayed` mode, time the work peers get before this node starts generating as well
	nano::work_peer_selection peer_selection{ nano::work_peer_selection::all };
	std::chrono::milliseconds peer_delay{ 15000 }; // With one work peer asked at first, time a peer gets before the next one is asked as well
	std::chrono::milliseconds peer_timeout{ 30000 }; // Time one work peer gets to answer one attempt
	std::chrono::milliseconds work_timeout{ 60000 }; // Time after which a request without work fails, on a node that does not generate work itself
	std::chrono::milliseconds connect_timeout{ 5000 }; // Time to resolve a work peer's name, and to connect to one of its addresses
	std::chrono::milliseconds cancel_timeout{ 2000 }; // Time a `work_cancel` sent to a work peer may take
	std::chrono::milliseconds retry_backoff{ 1000 }; // Time a work peer is left alone after it failed, doubling with every failure in a row
	std::chrono::milliseconds retry_backoff_max{ 30000 }; // Upper bound of that time
};

/**
 * Generates work for the node: asks the configured work peers, generates on the node's own work pool, or both, as the config says.
 * A request ends exactly once, with the first work that reaches its difficulty, as cancelled, or as failed once nothing is left that could deliver
 */
class work_generator final
{
public:
	// How often the requests in progress are checked for delays and deadlines that have run out
	static constexpr std::chrono::milliseconds poll_interval{ 50 };

public:
	work_generator (nano::node_config const &, nano::work_pool &, asio::io_context &, nano::stats &, nano::logger &);
	~work_generator ();

	void stop ();

	// Asks for work. The callback runs exactly once and never under a lock of the generator, a callback may call back into it
	void generate (nano::work_request const &, nano::work_callback);
	// Asks for work and waits for the result, never to be called from an io thread
	nano::work_result generate_blocking (nano::work_request const &);
	// Cancels the requests in progress for the root
	void cancel (nano::root const &);

	// Whether this node generates work itself
	bool local_enabled () const;
	// Whether anything could generate work for a request to the group, or for one that stays on this node
	bool enabled (std::optional<nano::work_peer_group> const & = nano::work_peer_group::primary) const;

	// Adds a work peer to a group unless it is in it already
	void add_peer (nano::work_peer const &, nano::work_peer_group = nano::work_peer_group::primary);
	void clear_peers (nano::work_peer_group = nano::work_peer_group::primary);
	std::deque<nano::work_peer> peers (nano::work_peer_group = nano::work_peer_group::primary) const;

	// Requests in progress
	size_t size () const;

	nano::container_info container_info () const;

public: // Events
	// A request ended, notified after its callback ran
	nano::observer_set<nano::work_summary const &> request_finished;

private: // Dependencies
	nano::work_generator_config const & config;
	nano::work_thresholds const & thresholds;
	nano::work_pool & pool;
	nano::stats & stats;
	nano::logger & logger;

private:
	enum class run_result
	{
		idle, // No request is in progress
		busy, // Requests are in progress
		stopped,
	};

	asio::awaitable<void> run (nano::async::condition &);
	// Applies what came in since the last round and does what the index asks for
	run_result run_one ();
	asio::awaitable<void> run_exchange (nano::work_generator_index::exchange_start);
	asio::awaitable<void> run_cancel (nano::work_generator_index::cancel_start);
	void start_local (nano::work_generator_index::local_start const &);
	void complete (nano::work_generator_index::completion const &);
	// Wakes the loop for something that came in, called with the mutex held
	void notify ();

	// The index limits as the node config sets them
	static nano::work_generator_index::limits index_limits (nano::node_config const &, nano::work_pool const &);

private:
	nano::work_peer_client client;

	nano::work_generator_index index; // Requests, exchanges, local generation and work peers, every access under `mutex`
	bool dirty{ false }; // Whether something came in that the loop has not looked at yet

	nano::async::strand strand;

	bool stopped{ false }; // Set by `stop`, after which requests are refused and the loop ends
	nano::condition_variable condition;
	mutable nano::mutex mutex;
	nano::async::task task;
};

nano::stat::detail to_stat_detail (nano::work_status);
}
