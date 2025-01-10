#include <celerix/lib/blocks.hpp>
#include <celerix/lib/stats.hpp>
#include <celerix/lib/threading.hpp>
#include <celerix/node/network.hpp>
#include <celerix/node/node.hpp>
#include <celerix/node/node_observers.hpp>
#include <celerix/node/telemetry.hpp>
#include <celerix/node/transport/transport.hpp>
#include <celerix/secure/ledger.hpp>

#include <boost/algorithm/string.hpp>

#include <algorithm>
#include <cstdint>
#include <future>
#include <numeric>
#include <set>

using namespace std::chrono_literals;

celerix::telemetry::telemetry (celerix::node_flags const & flags_a, celerix::node & node_a, celerix::network & network_a, celerix::node_observers & observers_a, celerix::network_params & network_params_a, celerix::stats & stats_a) :
	config{ flags_a },
	node{ node_a },
	network{ network_a },
	observers{ observers_a },
	network_params{ network_params_a },
	stats{ stats_a }
{
}

celerix::telemetry::~telemetry ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void celerix::telemetry::start ()
{
	debug_assert (!thread.joinable ());

	thread = std::thread ([this] () {
		celerix::thread_role::set (celerix::thread_role::name::telemetry);
		run ();
	});
}

void celerix::telemetry::stop ()
{
	{
		celerix::lock_guard<celerix::mutex> guard{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	celerix::join_or_pass (thread);
}

bool celerix::telemetry::verify (const celerix::telemetry_ack & telemetry, const std::shared_ptr<celerix::transport::channel> & channel) const
{
	if (telemetry.is_empty_payload ())
	{
		stats.inc (celerix::stat::type::telemetry, celerix::stat::detail::empty_payload);
		return false;
	}

	// Check if telemetry node id matches channel node id
	if (telemetry.data.node_id != channel->get_node_id ())
	{
		stats.inc (celerix::stat::type::telemetry, celerix::stat::detail::node_id_mismatch);
		return false;
	}

	// Check whether data is signed by node id presented in telemetry message
	if (telemetry.data.validate_signature ()) // Returns false when signature OK
	{
		stats.inc (celerix::stat::type::telemetry, celerix::stat::detail::invalid_signature);
		return false;
	}

	if (telemetry.data.genesis_block != network_params.ledger.genesis->hash ())
	{
		network.exclude (channel);

		stats.inc (celerix::stat::type::telemetry, celerix::stat::detail::genesis_mismatch);
		return false;
	}

	return true; // Telemetry is OK
}

void celerix::telemetry::process (const celerix::telemetry_ack & telemetry, const std::shared_ptr<celerix::transport::channel> & channel)
{
	if (!verify (telemetry, channel))
	{
		return;
	}

	celerix::unique_lock<celerix::mutex> lock{ mutex };

	if (auto it = telemetries.get<tag_channel> ().find (channel); it != telemetries.get<tag_channel> ().end ())
	{
		stats.inc (celerix::stat::type::telemetry, celerix::stat::detail::update);

		telemetries.get<tag_channel> ().modify (it, [&telemetry, &channel] (auto & entry) {
			entry.data = telemetry.data;
			entry.last_updated = std::chrono::steady_clock::now ();
		});
	}
	else
	{
		stats.inc (celerix::stat::type::telemetry, celerix::stat::detail::insert);
		telemetries.get<tag_channel> ().insert ({ channel, telemetry.data, std::chrono::steady_clock::now () });

		if (telemetries.size () > max_size)
		{
			stats.inc (celerix::stat::type::telemetry, celerix::stat::detail::overfill);
			telemetries.get<tag_sequenced> ().pop_front (); // Erase oldest entry
		}
	}

	lock.unlock ();

	observers.telemetry.notify (telemetry.data, channel);

	stats.inc (celerix::stat::type::telemetry, celerix::stat::detail::process);
}

void celerix::telemetry::trigger ()
{
	{
		celerix::lock_guard<celerix::mutex> guard{ mutex };
		triggered = true;
	}
	condition.notify_all ();
}

std::size_t celerix::telemetry::size () const
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };
	return telemetries.size ();
}

bool celerix::telemetry::request_predicate () const
{
	debug_assert (!mutex.try_lock ());

	if (triggered)
	{
		return true;
	}
	if (config.enable_ongoing_requests)
	{
		return last_request + network_params.network.telemetry_request_interval < std::chrono::steady_clock::now ();
	}
	return false;
}

bool celerix::telemetry::broadcast_predicate () const
{
	debug_assert (!mutex.try_lock ());

	if (config.enable_ongoing_broadcasts)
	{
		return last_broadcast + network_params.network.telemetry_broadcast_interval < std::chrono::steady_clock::now ();
	}
	return false;
}

void celerix::telemetry::run ()
{
	celerix::unique_lock<celerix::mutex> lock{ mutex };
	while (!stopped)
	{
		stats.inc (celerix::stat::type::telemetry, celerix::stat::detail::loop);

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

void celerix::telemetry::run_requests ()
{
	auto peers = network.list ();

	for (auto & channel : peers)
	{
		request (channel);
	}
}

void celerix::telemetry::request (std::shared_ptr<celerix::transport::channel> const & channel)
{
	stats.inc (celerix::stat::type::telemetry, celerix::stat::detail::request);

	celerix::telemetry_req message{ network_params.network };
	channel->send (message, celerix::transport::traffic_type::telemetry);
}

void celerix::telemetry::run_broadcasts ()
{
	auto telemetry = node.local_telemetry ();
	auto peers = network.list ();

	for (auto & channel : peers)
	{
		broadcast (channel, telemetry);
	}
}

void celerix::telemetry::broadcast (std::shared_ptr<celerix::transport::channel> const & channel, const celerix::telemetry_data & telemetry)
{
	stats.inc (celerix::stat::type::telemetry, celerix::stat::detail::broadcast);

	celerix::telemetry_ack message{ network_params.network, telemetry };
	channel->send (message, celerix::transport::traffic_type::telemetry);
}

void celerix::telemetry::cleanup ()
{
	debug_assert (!mutex.try_lock ());

	erase_if (telemetries, [this] (entry const & entry) {
		// Remove if telemetry data is stale
		if (!check_timeout (entry))
		{
			stats.inc (celerix::stat::type::telemetry, celerix::stat::detail::erase_stale);
			return true; // Erase
		}
		if (!entry.channel->alive ())
		{
			stats.inc (celerix::stat::type::telemetry, celerix::stat::detail::erase_dead);
			return true; // Erase
		}
		return false; // Do not erase
	});
}

bool celerix::telemetry::check_timeout (const entry & entry) const
{
	return entry.last_updated + network_params.network.telemetry_cache_cutoff >= std::chrono::steady_clock::now ();
}

std::optional<celerix::telemetry_data> celerix::telemetry::get_telemetry (const celerix::endpoint & endpoint) const
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };

	if (auto it = telemetries.get<tag_endpoint> ().find (endpoint); it != telemetries.get<tag_endpoint> ().end ())
	{
		if (check_timeout (*it))
		{
			return it->data;
		}
	}
	return {};
}

std::unordered_map<celerix::endpoint, celerix::telemetry_data> celerix::telemetry::get_all_telemetries () const
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };

	std::unordered_map<celerix::endpoint, celerix::telemetry_data> result;
	for (auto const & entry : telemetries)
	{
		if (check_timeout (entry))
		{
			result[entry.endpoint ()] = entry.data;
		}
	}
	return result;
}

celerix::container_info celerix::telemetry::container_info () const
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };

	celerix::container_info info;
	info.put ("telemetries", telemetries.size ());
	return info;
}