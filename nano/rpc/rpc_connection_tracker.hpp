#pragma once

#include <nano/lib/locks.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index_container.hpp>

#include <chrono>
#include <cstddef>
#include <memory>
#include <vector>

namespace mi = boost::multi_index;

namespace nano
{
class rpc_connection;

/**
 * Tracks the connections a server has accepted and which of them are serving a request,
 * so that stopping the server can close the idle ones and wait for the rest to finish.
 * Shared between the server and its connections, since connections may outlive the server.
 * A connection removes itself when it is destroyed, so no entry outlives its connection.
 */
class rpc_connection_tracker final
{
public:
	// Registers an accepted connection, idle until it reports a request
	void add (std::shared_ptr<nano::rpc_connection> const &);
	// Forgets a connection, called from its destructor
	void remove (nano::rpc_connection const &);

	// Reported by a connection when it starts and when it finishes serving a request
	void request_begin (nano::rpc_connection const &);
	void request_end (nano::rpc_connection const &);

	// Connections serving a request
	std::size_t in_flight () const;

	// Closes every connection that is not serving a request, blocks until they are closed
	void close_idle ();

	// Closes every connection, including those still serving a request, blocks until they are closed
	void close_all ();

	// Waits until no request is in flight, returns false if the timeout expires first
	bool wait_drained (std::chrono::milliseconds timeout);

private:
	struct entry
	{
		nano::rpc_connection const * key;
		std::weak_ptr<nano::rpc_connection> connection;
		bool serving{ false };
	};

	// clang-format off
	class tag_connection {};
	class tag_serving {};

	using ordered_entries = boost::multi_index_container<entry,
	mi::indexed_by<
		mi::hashed_unique<mi::tag<tag_connection>,
			mi::member<entry, nano::rpc_connection const *, &entry::key>>,
		mi::hashed_non_unique<mi::tag<tag_serving>,
			mi::member<entry, bool, &entry::serving>>>>;
	// clang-format on

	void set_serving (nano::rpc_connection const &, bool serving);

	// Strong references to the connections still alive, all of them or only the idle ones; caller holds the mutex
	std::vector<std::shared_ptr<nano::rpc_connection>> lock_all () const;
	std::vector<std::shared_ptr<nano::rpc_connection>> lock_idle () const;

	mutable nano::mutex mutex;
	nano::condition_variable condition;
	ordered_entries entries;
};
}
