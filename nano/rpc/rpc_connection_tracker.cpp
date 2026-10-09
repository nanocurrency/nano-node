#include <nano/lib/assert.hpp>
#include <nano/rpc/rpc_connection.hpp>
#include <nano/rpc/rpc_connection_tracker.hpp>

void nano::rpc_connection_tracker::add (std::shared_ptr<nano::rpc_connection> const & connection)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	auto inserted = entries.insert (entry{ connection.get (), connection }).second;
	debug_assert (inserted);
}

void nano::rpc_connection_tracker::remove (nano::rpc_connection const & connection)
{
	bool was_serving{ false };
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		auto & index = entries.get<tag_connection> ();
		if (auto it = index.find (&connection); it != index.end ())
		{
			was_serving = it->serving;
			index.erase (it);
		}
	}
	// A connection that went away mid-request is no longer in flight, whether or not it reported the end
	if (was_serving)
	{
		condition.notify_all ();
	}
}

void nano::rpc_connection_tracker::request_begin (nano::rpc_connection const & connection)
{
	set_serving (connection, true);
}

void nano::rpc_connection_tracker::request_end (nano::rpc_connection const & connection)
{
	set_serving (connection, false);
	condition.notify_all ();
}

void nano::rpc_connection_tracker::set_serving (nano::rpc_connection const & connection, bool serving)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	auto & index = entries.get<tag_connection> ();
	auto it = index.find (&connection);
	debug_assert (it != index.end ());
	if (it != index.end ())
	{
		index.modify (it, [serving] (entry & item) { item.serving = serving; });
	}
}

std::size_t nano::rpc_connection_tracker::in_flight () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return entries.get<tag_serving> ().count (true);
}

void nano::rpc_connection_tracker::close_idle ()
{
	// The strong references must outlive the lock: closing blocks on the connection's strand, and a connection released here unregisters itself under the same mutex
	std::vector<std::shared_ptr<nano::rpc_connection>> idle;
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		idle = lock_idle ();
	}
	for (auto const & connection : idle)
	{
		connection->close ();
	}
}

void nano::rpc_connection_tracker::close_all ()
{
	std::vector<std::shared_ptr<nano::rpc_connection>> all;
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		all = lock_all ();
	}
	for (auto const & connection : all)
	{
		connection->close ();
	}
}

bool nano::rpc_connection_tracker::wait_drained (std::chrono::milliseconds timeout)
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	return condition.wait_for (lock, timeout, [this] () {
		auto const & index = entries.get<tag_serving> ();
		return index.find (true) == index.end ();
	});
}

std::vector<std::shared_ptr<nano::rpc_connection>> nano::rpc_connection_tracker::lock_all () const
{
	debug_assert (!mutex.try_lock ());
	std::vector<std::shared_ptr<nano::rpc_connection>> result;
	for (auto const & item : entries)
	{
		if (auto connection = item.connection.lock ())
		{
			result.push_back (std::move (connection));
		}
	}
	return result;
}

std::vector<std::shared_ptr<nano::rpc_connection>> nano::rpc_connection_tracker::lock_idle () const
{
	debug_assert (!mutex.try_lock ());
	std::vector<std::shared_ptr<nano::rpc_connection>> result;
	auto const & index = entries.get<tag_serving> ();
	auto range = index.equal_range (false);
	for (auto it = range.first; it != range.second; ++it)
	{
		if (auto connection = it->connection.lock ())
		{
			result.push_back (std::move (connection));
		}
	}
	return result;
}
