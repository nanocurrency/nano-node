#include <nano/lib/blocks.hpp>
#include <nano/lib/enum_util.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/lib/threading.hpp>
#include <nano/messages/telemetry.hpp>
#include <nano/node/network.hpp>
#include <nano/node/node.hpp>
#include <nano/node/node_observers.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/telemetry.hpp>
#include <nano/node/transport/tcp_config.hpp>
#include <nano/node/transport/transport.hpp>
#include <nano/secure/ledger.hpp>

#include <boost/algorithm/string.hpp>

#include <algorithm>
#include <cstdint>
#include <future>
#include <numeric>
#include <set>

using namespace std::chrono_literals;

nano::telemetry_config::telemetry_config (nano::node_flags const & flags, nano::network_constants const & network, nano::transport::tcp_config const & tcp) :
	enable_ongoing_broadcasts{ !flags.disable_providing_telemetry_metrics },
	// Room for a live channel in every connection slot and as many closed ones awaiting cleanup
	max_size{ 2 * (tcp.max_inbound_connections + tcp.max_outbound_connections) },
	cache_cutoff{ network.telemetry_cache_cutoff }
{
}

nano::telemetry::telemetry (nano::node_flags const & flags_a, nano::node & node_a, nano::network & network_a, nano::node_observers & observers_a, nano::network_params & network_params_a, nano::stats & stats_a) :
	config{ flags_a, network_params_a.network, *node_a.config.tcp },
	node{ node_a },
	network{ network_a },
	observers{ observers_a },
	network_params{ network_params_a },
	stats{ stats_a },
	telemetries{ config }
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

bool nano::telemetry::verify (const nano::messages::telemetry_ack & telemetry, const std::shared_ptr<nano::transport::channel> & channel) const
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

bool nano::telemetry::process (const nano::messages::telemetry_ack & telemetry, const std::shared_ptr<nano::transport::channel> & channel)
{
	if (!verify (telemetry, channel))
	{
		return false;
	}

	nano::unique_lock<nano::mutex> lock{ mutex };
	auto const result = telemetries.put (channel, telemetry.data, std::chrono::steady_clock::now ());
	lock.unlock ();

	// Making room still adds an entry for the new channel
	if (result == nano::telemetry_index::result::erase_dead || result == nano::telemetry_index::result::overfill)
	{
		stats.inc (nano::stat::type::telemetry, nano::stat::detail::insert);
	}
	stats.inc (nano::stat::type::telemetry, to_stat_detail (result));

	observers.telemetry.notify (telemetry.data, channel);

	stats.inc (nano::stat::type::telemetry, nano::stat::detail::process);
	return true;
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
	if (config.enable_ongoing_requests)
	{
		return last_request + network_params.network.telemetry_request_interval < std::chrono::steady_clock::now ();
	}
	return false;
}

bool nano::telemetry::broadcast_predicate () const
{
	debug_assert (!mutex.try_lock ());

	if (config.enable_ongoing_broadcasts)
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

void nano::telemetry::request (std::shared_ptr<nano::transport::channel> const & channel)
{
	stats.inc (nano::stat::type::telemetry, nano::stat::detail::request);

	nano::messages::telemetry_req message{ network_params.network };
	channel->send (message, nano::transport::traffic_type::telemetry);
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

void nano::telemetry::broadcast (std::shared_ptr<nano::transport::channel> const & channel, const nano::messages::telemetry_data & telemetry)
{
	stats.inc (nano::stat::type::telemetry, nano::stat::detail::broadcast);

	nano::messages::telemetry_ack message{ network_params.network, telemetry };
	channel->send (message, nano::transport::traffic_type::telemetry);
}

void nano::telemetry::cleanup ()
{
	debug_assert (!mutex.try_lock ());

	auto const result = telemetries.cleanup (std::chrono::steady_clock::now ());
	stats.add (nano::stat::type::telemetry, nano::stat::detail::erase_stale, result.stale);
	stats.add (nano::stat::type::telemetry, nano::stat::detail::erase_dead, result.dead);
}

std::optional<nano::messages::telemetry_data> nano::telemetry::get_telemetry (const nano::endpoint & endpoint) const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return telemetries.get (endpoint, std::chrono::steady_clock::now ());
}

std::unordered_map<nano::endpoint, nano::messages::telemetry_data> nano::telemetry::get_all_telemetries () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return telemetries.get_all (std::chrono::steady_clock::now ());
}

nano::container_info nano::telemetry::container_info () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };

	nano::container_info info;
	info.put ("telemetries", telemetries.size ());
	return info;
}

/*
 * telemetry_index
 */

nano::telemetry_index::telemetry_index (telemetry_config const & config_a) :
	config{ config_a }
{
}

auto nano::telemetry_index::put (std::shared_ptr<nano::transport::channel> const & channel, nano::messages::telemetry_data const & data, std::chrono::steady_clock::time_point now) -> result
{
	auto & sequenced = telemetries.get<tag_sequenced> ();
	auto & channels = telemetries.get<tag_channel> ();

	if (auto it = channels.find (channel); it != channels.end ())
	{
		channels.modify (it, [&data, now] (auto & entry) {
			entry.data = data;
			entry.last_updated = now;
		});
		sequenced.relocate (sequenced.end (), telemetries.project<tag_sequenced> (it));
		return result::update;
	}

	channels.insert ({ channel, data, now });
	if (telemetries.size () <= config.max_size)
	{
		return result::insert;
	}

	// A closed channel goes before any live one, so churned connections cannot push out long-lived peers
	if (auto it = std::find_if (sequenced.begin (), sequenced.end (), [] (entry const & entry) { return !entry.channel->alive (); }); it != sequenced.end ())
	{
		sequenced.erase (it);
		return result::erase_dead;
	}
	sequenced.pop_front ();
	return result::overfill;
}

auto nano::telemetry_index::cleanup (std::chrono::steady_clock::time_point now) -> cleanup_result
{
	cleanup_result result;
	erase_if (telemetries, [this, now, &result] (entry const & entry) {
		if (!check_timeout (entry, now))
		{
			++result.stale;
			return true; // Erase
		}
		if (!entry.channel->alive ())
		{
			++result.dead;
			return true; // Erase
		}
		return false; // Do not erase
	});
	return result;
}

std::optional<nano::messages::telemetry_data> nano::telemetry_index::get (nano::endpoint const & endpoint, std::chrono::steady_clock::time_point now) const
{
	if (auto it = telemetries.get<tag_endpoint> ().find (endpoint); it != telemetries.get<tag_endpoint> ().end ())
	{
		if (check_timeout (*it, now))
		{
			return it->data;
		}
	}
	return {};
}

std::unordered_map<nano::endpoint, nano::messages::telemetry_data> nano::telemetry_index::get_all (std::chrono::steady_clock::time_point now) const
{
	std::unordered_map<nano::endpoint, nano::messages::telemetry_data> result;
	for (auto const & entry : telemetries)
	{
		if (check_timeout (entry, now))
		{
			result[entry.endpoint ()] = entry.data;
		}
	}
	return result;
}

bool nano::telemetry_index::contains (std::shared_ptr<nano::transport::channel> const & channel) const
{
	return telemetries.get<tag_channel> ().contains (channel);
}

std::size_t nano::telemetry_index::size () const
{
	return telemetries.size ();
}

bool nano::telemetry_index::check_timeout (entry const & entry, std::chrono::steady_clock::time_point now) const
{
	return entry.last_updated + config.cache_cutoff >= now;
}

nano::endpoint nano::telemetry_index::entry::endpoint () const
{
	return channel->get_remote_endpoint ();
}

/*
 *
 */

nano::stat::detail nano::to_stat_detail (nano::telemetry_index::result result)
{
	return nano::enum_convert<nano::stat::detail> (result);
}