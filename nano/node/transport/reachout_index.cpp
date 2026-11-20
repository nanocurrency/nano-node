#include <nano/lib/container_info.hpp>
#include <nano/node/transport/reachout_index.hpp>
#include <nano/node/transport/transport.hpp>

#include <algorithm>

/*
 * reachout
 */

nano::transport::reachout::reachout (nano::tcp_endpoint const & endpoint_a, unsigned failures_a, std::chrono::steady_clock::time_point start_a, std::chrono::steady_clock::time_point expiry_a) :
	endpoint{ endpoint_a },
	address{ nano::transport::ipv4_address_or_ipv6_subnet (endpoint_a.address ()) },
	subnetwork{ nano::transport::map_address_to_subnetwork (endpoint_a.address ()) },
	start{ start_a },
	expiry{ expiry_a },
	failures{ failures_a }
{
}

void nano::transport::reachout::mark_connected ()
{
	resolve (outcome::connected);
}

void nano::transport::reachout::drop ()
{
	resolve (outcome::dropped);
}

auto nano::transport::reachout::result () const -> outcome
{
	return state;
}

void nano::transport::reachout::resolve (outcome value)
{
	// The first outcome stands, a later report changes nothing
	auto expected = outcome::pending;
	state.compare_exchange_strong (expected, value);
}

/*
 * reachout_index
 */

nano::transport::reachout_index::reachout_index (limits const & limits_a) :
	config{ limits_a }
{
}

auto nano::transport::reachout_index::track (nano::tcp_endpoint const & endpoint, std::chrono::steady_clock::time_point now) -> std::shared_ptr<reachout>
{
	auto & by_endpoint = reachouts.get<tag_endpoint> ();
	auto const existing = by_endpoint.find (endpoint);

	unsigned failures = 0;
	if (existing != by_endpoint.end ())
	{
		// An earlier reachout still holds the endpoint
		auto const & previous = **existing;
		if (holding (previous, now))
		{
			return nullptr;
		}

		// A channel clears the failures, a dropped reachout says nothing about the endpoint and leaves them, a failed one adds one
		auto const result = previous.result ();
		if (result == reachout::outcome::connected)
		{
			failures = 0;
		}
		else if (result == reachout::outcome::dropped)
		{
			failures = previous.failures;
		}
		else
		{
			failures = previous.failures + 1;
		}

		by_endpoint.erase (existing);
	}

	// The new record holds the endpoint while its attempt runs and for as long as its failures call for
	auto record = std::make_shared<reachout> (endpoint, failures, now, now + hold (failures));
	reachouts.insert (record);

	// The attempt gets a handle of its own: the record stays with the index, and the last copy of the handle going without an outcome is the failure report
	auto const fail_unresolved = [record] (reachout *) {
		record->resolve (reachout::outcome::failed);
	};

	return std::shared_ptr<reachout>{ record.get (), fail_unresolved };
}

size_t nano::transport::reachout_index::purge (std::chrono::steady_clock::time_point now)
{
	auto & by_expiry = reachouts.get<tag_expiry> ();

	// An expired record is kept for its failures until the longest hold has passed once more
	by_expiry.erase (by_expiry.begin (), by_expiry.upper_bound (now - config.cooldown_max));

	// Beyond the size limit the records expiring first go
	size_t evicted = 0;
	while (reachouts.size () > config.max_size)
	{
		by_expiry.erase (by_expiry.begin ());
		++evicted;
	}

	return evicted;
}

size_t nano::transport::reachout_index::count_per_ip (boost::asio::ip::address const & address, std::chrono::steady_clock::time_point now) const
{
	auto const range = reachouts.get<tag_address> ().equal_range (nano::transport::ipv4_address_or_ipv6_subnet (address));
	return std::count_if (range.first, range.second, [now] (auto const & entry) {
		return holding (*entry, now);
	});
}

size_t nano::transport::reachout_index::count_per_subnetwork (boost::asio::ip::address const & address, std::chrono::steady_clock::time_point now) const
{
	auto const range = reachouts.get<tag_subnetwork> ().equal_range (nano::transport::map_address_to_subnetwork (address));
	return std::count_if (range.first, range.second, [now] (auto const & entry) {
		return holding (*entry, now);
	});
}

size_t nano::transport::reachout_index::size () const
{
	return reachouts.size ();
}

nano::container_info nano::transport::reachout_index::container_info () const
{
	nano::container_info info;
	info.put ("reachouts", reachouts.size ());
	return info;
}

std::chrono::seconds nano::transport::reachout_index::hold (unsigned failures) const
{
	// Double per failure, stopping once the longest hold is reached
	auto result = config.cooldown;
	for (unsigned i = 0; i < failures && result > 0s && result < config.cooldown_max; ++i)
	{
		result *= 2;
	}

	return std::min (result, config.cooldown_max);
}

bool nano::transport::reachout_index::holding (reachout const & entry, std::chrono::steady_clock::time_point now)
{
	auto const result = entry.result ();

	// A running attempt holds its endpoint whatever the expiry says
	if (result == reachout::outcome::pending)
	{
		return true;
	}

	// A refusal by this node frees it at once
	if (result == reachout::outcome::dropped)
	{
		return false;
	}

	return now < entry.expiry;
}
