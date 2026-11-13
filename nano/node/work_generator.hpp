#pragma once

#include <nano/lib/async.hpp>
#include <nano/lib/fwd.hpp>
#include <nano/lib/locks.hpp>
#include <nano/lib/numbers_templ.hpp>
#include <nano/lib/observer_set.hpp>
#include <nano/node/fwd.hpp>
#include <nano/node/work_peer_client.hpp>
#include <nano/node/work_request.hpp>

#include <boost/asio.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

namespace mi = boost::multi_index;
namespace asio = boost::asio;

namespace nano
{
// When this node generates work itself for a request that can also go to work peers
enum class work_local_mode
{
	fallback, // Once every work peer has failed for the request or has run out of time for its attempt
	delayed, // Like `fallback`, and also once the work peers have not delivered within `local_delay`
	always, // From the start, alongside the work peers
	never, // Never, only work peers generate work
};

// How the work peers of a request's group are asked
enum class work_peer_selection
{
	all, // All at once, the first work that arrives is used
	one, // One at first, each request starting with the next peer in turn, and the next one as well when it fails or takes long
};

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
	std::chrono::milliseconds connect_timeout{ 5000 }; // Time to resolve and connect to a work peer
	std::chrono::milliseconds cancel_timeout{ 2000 }; // Time a `work_cancel` sent to a work peer may take
	std::chrono::milliseconds retry_backoff{ 1000 }; // Time a work peer is left alone after it failed, doubling with every failure in a row
	std::chrono::milliseconds retry_backoff_max{ 30000 }; // Upper bound of that time
};

/**
 * The work requests in progress with their exchanges and local generation, and the work peers with how they have been doing.
 * It decides what each request does next. It holds no lock or clock and never touches the tasks it carries, the generator drives it under its own mutex and tests drive it directly
 */
class work_generator_index final
{
public:
	using id_t = uint64_t;
	using time_point = std::chrono::steady_clock::time_point;

	// What the index decides by, filled by the generator from the node config
	struct limits
	{
		nano::work_local_mode local_generation{ nano::work_local_mode::fallback }; // When this node generates work itself
		bool local_available{ true }; // Whether this node has anything to generate work with
		std::chrono::milliseconds local_delay{ 5000 }; // In `delayed` mode, time the work peers get before this node generates as well
		nano::work_peer_selection peer_selection{ nano::work_peer_selection::all }; // How the work peers of a request's group are asked
		std::chrono::milliseconds peer_delay{ 15000 }; // With one work peer asked at first, time a peer gets before the next one is asked as well
		std::chrono::milliseconds peer_timeout{ 30000 }; // Time one work peer gets to answer one attempt
		std::chrono::milliseconds work_timeout{ 60000 }; // Time after which a request without work fails, on a node that does not generate work itself
		std::chrono::milliseconds retry_backoff{ 1000 }; // Time a work peer is left alone after it failed, doubling with every failure in a row
		std::chrono::milliseconds retry_backoff_max{ 30000 }; // Upper bound of that time
	};

	// A `work_generate` or `work_cancel` on its way to a work peer
	struct exchange
	{
		id_t id;
		id_t request; // The request it asks work for, 0 for a `work_cancel`
		nano::root root;
		nano::work_peer peer;
		std::shared_ptr<nano::async::task> task; // The coroutine running it
		bool finished{ false }; // Whether its outcome is in, a `work_cancel` has none to wait for
	};

	// A `work_generate` exchange for the generator to start
	struct exchange_start
	{
		id_t id; // Id of the exchange, to attach its task and to report its outcome
		nano::work_request request;
		nano::work_peer peer;
		time_point deadline; // When the peer's time for this attempt is over
	};

	// A `work_cancel` exchange for the generator to start
	struct cancel_start
	{
		id_t id; // Id of the exchange, to attach its task
		nano::work_peer peer;
		nano::root root;
	};

	// Local generation for the generator to start
	struct local_start
	{
		id_t request;
		nano::work_request work;
	};

	// A request that ended
	struct completion
	{
		nano::work_callback callback;
		nano::work_summary summary;
	};

	// What the generator has to do after the requests moved forward
	struct actions
	{
		std::deque<exchange_start> exchanges;
		std::deque<cancel_start> cancels;
		std::deque<local_start> locals;
		std::deque<std::shared_ptr<nano::async::task>> abandoned; // Exchanges of ended requests, to cancel
		std::deque<uint64_t> abandoned_locals; // Work pool items of ended requests, to cancel
		std::deque<completion> completions;
	};

public:
	explicit work_generator_index (limits const &);

	// Adds a request, empty once the index has been stopped
	std::optional<id_t> insert (nano::work_request const &, nano::work_callback, time_point now);
	// Marks the requests for the root as cancelled and returns how many there are
	size_t cancel (nano::root const &);
	// Moves every request as far as it can go, ends the ones that are done and says what there is to do
	actions advance (time_point now);
	// Refuses new requests from now on and ends the ones in progress as cancelled
	actions stop (time_point now);
	bool stopped () const;

	// Hands the task of an exchange that `advance` started to the index
	void attach (id_t exchange, std::shared_ptr<nano::async::task>);
	// Records how a `work_generate` exchange ended
	void exchange_finished (id_t exchange, nano::work_peer_response const &, time_point now);
	// Removes the exchanges the predicate selects and hands them back
	std::deque<exchange> erase_exchanges_if (std::function<bool (exchange const &)> const &);
	std::deque<exchange> list_exchanges () const;

	// Records the work pool item of a request's local generation, false if the request no longer wants it
	bool local_started (id_t request, uint64_t pool_id);
	// Records how local generation for a request ended
	void local_finished (id_t request, std::optional<uint64_t> const & work);

	// Adds a work peer to a group unless it is in it already
	void add_peer (nano::work_peer_group, nano::work_peer const &);
	void clear_peers (nano::work_peer_group);
	std::deque<nano::work_peer> peers (nano::work_peer_group) const;

	// Whether this node generates work itself
	bool local_enabled () const;
	// Whether anything could generate work for a request to the group, or for one that stays on this node
	bool enabled (std::optional<nano::work_peer_group> const &) const;

	// Requests in progress
	size_t size () const;
	// Exchanges with work peers that are not removed yet
	size_t exchange_count () const;
	// Local generation the work pool has not answered yet
	size_t local_count () const;

	nano::container_info container_info () const;

private:
	struct request_entry
	{
		id_t id;
		nano::work_request work;
		nano::work_callback callback;
		time_point start; // When the request came in
		bool cancelled{ false }; // Whether a cancel for its root came in
		bool local_spent{ false }; // Whether local generation ran and ended without work
		std::optional<uint64_t> found; // The work, once anything delivered it
		std::string source; // What delivered the work
		std::deque<std::string> bad_peers; // Work peers that failed for this request
		std::optional<size_t> next_peer; // With one work peer asked at first, position in the group of the peer to ask next
		time_point asked_timestamp{}; // With one work peer asked at first, when the last one was asked
		bool peer_failed{ false }; // With one work peer asked at first, whether one failed for the request since the last one was asked

		nano::root root () const;
	};

	struct local_entry
	{
		id_t request;
		std::optional<uint64_t> pool_id; // The work pool item, empty until the generator has queued it
		bool abandoned{ false }; // Whether the request ended before the item was known
	};

	struct peer_entry
	{
		nano::work_peer peer;
		unsigned failures{ 0 }; // Failures in a row
		time_point retry_timestamp{}; // The peer is not asked before this
	};

	using ordered_peers = std::deque<peer_entry>;

	// clang-format off
	class tag_sequenced {};
	class tag_id {};
	class tag_root {};
	class tag_request {};

	using ordered_requests = boost::multi_index_container<request_entry,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_unique<mi::tag<tag_id>,
			mi::member<request_entry, id_t, &request_entry::id>>,
		mi::hashed_non_unique<mi::tag<tag_root>,
			mi::const_mem_fun<request_entry, nano::root, &request_entry::root>>
	>>;

	using ordered_exchanges = boost::multi_index_container<exchange,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_unique<mi::tag<tag_id>,
			mi::member<exchange, id_t, &exchange::id>>,
		mi::hashed_non_unique<mi::tag<tag_request>,
			mi::member<exchange, id_t, &exchange::request>>
	>>;

	using ordered_locals = boost::multi_index_container<local_entry,
	mi::indexed_by<
		mi::hashed_unique<mi::tag<tag_request>,
			mi::member<local_entry, id_t, &local_entry::request>>
	>>;
	// clang-format on

private:
	// Starts what the request can start now, and says how it ended if it did
	std::optional<nano::work_status> advance_request (request_entry const &, time_point now, actions &);
	// Removes a request and adds what its end takes to the actions
	void finish (request_entry const &, nano::work_status, time_point now, actions &);
	// Asks every work peer of the request's group that can be asked now, true if one was left out because it rests
	bool ask_all_peers (request_entry const &, time_point now, actions &);
	// Asks the next work peer in turn when one more is due for the request, true if one was left out because it rests
	bool ask_next_peer (request_entry const &, time_point now, actions &);

	ordered_peers & peers_of (nano::work_peer_group);
	ordered_peers const & peers_of (nano::work_peer_group) const;
	// With one work peer asked at first, position in the group where its next new request starts
	size_t & rotation_of (nano::work_peer_group);
	// Exchanges of the request whose outcome is not in yet
	size_t count_exchanges (id_t request) const;
	// Whether the request has an exchange with the peer whose outcome is not in yet
	bool has_exchange (id_t request, nano::work_peer const &) const;
	// Whether any request has the peer working on the root
	bool has_exchange (nano::root const &, nano::work_peer const &) const;
	// Whether every work peer of the request's group has failed for it, or rests after a failure for another request
	bool peers_exhausted (request_entry const &, time_point now) const;

private:
	limits const config;

	ordered_requests requests; // Requests in progress, in the order they came in
	ordered_exchanges exchanges; // Exchanges with work peers, kept until the generator removes them
	ordered_locals locals; // Local generation the work pool has not answered yet, which can outlive its request

	ordered_peers primary; // The `work_peers` and how they have been doing
	ordered_peers secondary; // The `secondary_work_peers` and how they have been doing
	size_t primary_rotation{ 0 }; // With one work peer asked at first, position in `primary` where the next new request starts
	size_t secondary_rotation{ 0 }; // The same position in `secondary`

	// `work_cancel` exchanges that the next `advance` records and hands out for starting
	std::deque<cancel_start> pending_cancels;

	id_t next_id{ 1 }; // Shared by requests and exchanges
	bool stopped_m{ false };
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

	nano::condition_variable condition;
	mutable nano::mutex mutex;
	nano::async::task task;
};

std::string_view to_string (nano::work_local_mode);
std::string_view to_string (nano::work_peer_selection);
nano::stat::detail to_stat_detail (nano::work_status);
}
