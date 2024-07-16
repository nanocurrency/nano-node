#include <nano/lib/blocks.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/threading.hpp>
#include <nano/node/network.hpp>
#include <nano/node/node.hpp>
#include <nano/node/node_observers.hpp>
#include <nano/node/telemetry.hpp>
#include <nano/node/transport/transport.hpp>
#include <nano/secure/ledger.hpp>

#include <boost/algorithm/string.hpp>

#include <algorithm>
#include <cstdint>
#include <future>
#include <numeric>
#include <set>

#include <fmt/core.h>

using namespace std::chrono_literals;

nano::telemetry::telemetry (const config & config_a, nano::node & node_a, nano::network & network_a, nano::node_observers & observers_a, nano::network_params & network_params_a, nano::stats & stats_a) :
	config_m{ config_a },
	node{ node_a },
	network{ network_a },
	observers{ observers_a },
	network_params{ network_params_a },
	stats{ stats_a }
{
}

nano::telemetry::~telemetry ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void nano::telemetry::start ()
{
	debug_assert (!thread.joinable ());

	thread = std::thread ([this] () {
		nano::thread_role::set (nano::thread_role::name::telemetry);
		run ();
	});
}

void nano::telemetry::stop ()
{
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	nano::join_or_pass (thread);
}

bool nano::telemetry::verify (const nano::telemetry_ack & telemetry, const std::shared_ptr<nano::transport::channel> & channel) const
{
	if (telemetry.is_empty_payload ())
	{
		stats.inc (nano::stat::type::telemetry, nano::stat::detail::empty_payload);
		return false;
	}

	// Check if telemetry node id matches channel node id
	if (telemetry.data.node_id != channel->get_node_id ())
	{
		stats.inc (nano::stat::type::telemetry, nano::stat::detail::node_id_mismatch);
		return false;
	}

	// Check whether data is signed by node id presented in telemetry message
	if (telemetry.data.validate_signature ()) // Returns false when signature OK
	{
		stats.inc (nano::stat::type::telemetry, nano::stat::detail::invalid_signature);
		return false;
	}

	if (telemetry.data.genesis_block != network_params.ledger.genesis->hash ())
	{
		network.exclude (channel);

		stats.inc (nano::stat::type::telemetry, nano::stat::detail::genesis_mismatch);
		return false;
	}

	return true; // Telemetry is OK
}

void nano::telemetry::process (const nano::telemetry_ack & telemetry, const std::shared_ptr<nano::transport::channel> & channel)
{
	if (!verify (telemetry, channel))
	{
		return;
	}

	nano::unique_lock<nano::mutex> lock{ mutex };

	const auto endpoint = channel->get_endpoint ();

	if (auto it = telemetries.get<tag_endpoint> ().find (endpoint); it != telemetries.get<tag_endpoint> ().end ())
	{
		stats.inc (nano::stat::type::telemetry, nano::stat::detail::update);

		telemetries.get<tag_endpoint> ().modify (it, [&telemetry, &endpoint] (auto & entry) {
			debug_assert (entry.endpoint == endpoint);
			entry.data = telemetry.data;
			entry.last_updated = std::chrono::steady_clock::now ();
		});
	}
	else
	{
		stats.inc (nano::stat::type::telemetry, nano::stat::detail::insert);
		telemetries.get<tag_endpoint> ().insert ({ endpoint, telemetry.data, std::chrono::steady_clock::now (), channel });

		if (telemetries.size () > max_size)
		{
			stats.inc (nano::stat::type::telemetry, nano::stat::detail::overfill);
			telemetries.get<tag_sequenced> ().pop_front (); // Erase oldest entry
		}
	}

	lock.unlock ();

	observers.telemetry.notify (telemetry.data, channel);

	stats.inc (nano::stat::type::telemetry, nano::stat::detail::process);
}

void nano::telemetry::trigger ()
{
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		triggered = true;
	}
	condition.notify_all ();
}

std::size_t nano::telemetry::size () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return telemetries.size ();
}

bool nano::telemetry::request_predicate () const
{
	debug_assert (!mutex.try_lock ());

	if (triggered)
	{
		return true;
	}
	if (config_m.enable_ongoing_requests)
	{
		return last_request + network_params.network.telemetry_request_interval < std::chrono::steady_clock::now ();
	}
	return false;
}

bool nano::telemetry::broadcast_predicate () const
{
	debug_assert (!mutex.try_lock ());

	if (config_m.enable_ongoing_broadcasts)
	{
		return last_broadcast + network_params.network.telemetry_broadcast_interval < std::chrono::steady_clock::now ();
	}
	return false;
}

void nano::telemetry::run ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		stats.inc (nano::stat::type::telemetry, nano::stat::detail::loop);

		cleanup ();

		if (request_predicate ())
		{
			triggered = false;
			lock.unlock ();

			run_requests ();

			lock.lock ();
			last_request = std::chrono::steady_clock::now ();
		}

		if (broadcast_predicate ())
		{
			lock.unlock ();

			run_broadcasts ();

			lock.lock ();
			last_broadcast = std::chrono::steady_clock::now ();
		}

		condition.wait_for (lock, std::min (network_params.network.telemetry_request_interval, network_params.network.telemetry_broadcast_interval) / 2);
	}
}

void nano::telemetry::run_requests ()
{
	auto peers = network.list ();

	for (auto & channel : peers)
	{
		request (channel);
	}
}

void nano::telemetry::request (std::shared_ptr<nano::transport::channel> & channel)
{
	stats.inc (nano::stat::type::telemetry, nano::stat::detail::request);

	nano::telemetry_req message{ network_params.network };
	channel->send (message);
}

void nano::telemetry::run_broadcasts ()
{
	auto telemetry = node.local_telemetry ();
	auto peers = network.list ();

	for (auto & channel : peers)
	{
		broadcast (channel, telemetry);
	}
}

void nano::telemetry::broadcast (std::shared_ptr<nano::transport::channel> & channel, const nano::telemetry_data & telemetry)
{
	stats.inc (nano::stat::type::telemetry, nano::stat::detail::broadcast);

	nano::telemetry_ack message{ network_params.network, telemetry };
	channel->send (message);
}

void nano::telemetry::cleanup ()
{
	debug_assert (!mutex.try_lock ());

	erase_if (telemetries, [this] (entry const & entry) {
		// Remove if telemetry data is stale
		if (!check_timeout (entry))
		{
			stats.inc (nano::stat::type::telemetry, nano::stat::detail::cleanup_outdated);
			return true; // Erase
		}

		return false; // Do not erase
	});
}

bool nano::telemetry::check_timeout (const entry & entry) const
{
	return entry.last_updated + network_params.network.telemetry_cache_cutoff >= std::chrono::steady_clock::now ();
}

std::optional<nano::telemetry_data> nano::telemetry::get_telemetry (const nano::endpoint & endpoint) const
{
	nano::lock_guard<nano::mutex> guard{ mutex };

	if (auto it = telemetries.get<tag_endpoint> ().find (endpoint); it != telemetries.get<tag_endpoint> ().end ())
	{
		if (check_timeout (*it))
		{
			return it->data;
		}
	}
	return {};
}

std::unordered_map<nano::endpoint, nano::telemetry_data> nano::telemetry::get_all_telemetries () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };

	std::unordered_map<nano::endpoint, nano::telemetry_data> result;
	for (auto const & entry : telemetries)
	{
		if (check_timeout (entry))
		{
			result[entry.endpoint] = entry.data;
		}
	}
	return result;
}

std::unique_ptr<nano::container_info_component> nano::telemetry::collect_container_info (const std::string & name)
{
	nano::lock_guard<nano::mutex> guard{ mutex };

	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "telemetries", telemetries.size (), sizeof (decltype (telemetries)::value_type) }));
	return composite;
}
