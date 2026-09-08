#include <nano/crypto_lib/random_pool.hpp>
#include <nano/lib/container_info.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/node_capabilities.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/tomlconfig.hpp>
#include <nano/lib/utility.hpp>
#include <nano/lib/vote.hpp>
#include <nano/node/network.hpp>
#include <nano/node/transport/channel.hpp>
#include <nano/node/transport/formatting.hpp>
#include <nano/node/transport/traffic_type.hpp>
#include <nano/node/vote_processor.hpp>
#include <nano/node/vote_relay_client.hpp>
#include <nano/node/vote_router.hpp>

#include <limits>

/*
 * vote_relay_client_index
 */

bool nano::vote_relay_client_index::insert (entry const & entry_a)
{
	auto [it, inserted] = entries.insert (entry_a);
	return inserted;
}

std::optional<nano::vote_relay_client_index::entry> nano::vote_relay_client_index::find (id_t id, std::shared_ptr<nano::transport::channel> const & channel) const
{
	auto const & by_id = entries.get<tag_id> ();
	if (auto existing = by_id.find (id); existing != by_id.end () && existing->channel == channel)
	{
		return *existing;
	}
	return std::nullopt;
}

void nano::vote_relay_client_index::received (id_t id, std::size_t votes)
{
	auto & by_id = entries.get<tag_id> ();
	if (auto existing = by_id.find (id); existing != by_id.end ())
	{
		by_id.modify (existing, [votes] (entry & e) {
			e.votes += votes;
		});
	}
}

bool nano::vote_relay_client_index::erase (id_t id)
{
	return entries.get<tag_id> ().erase (id) > 0;
}

std::vector<nano::vote_relay_client_index::entry> nano::vote_relay_client_index::evict (std::chrono::steady_clock::time_point now)
{
	auto & by_deadline = entries.get<tag_deadline> ();
	auto const end = by_deadline.upper_bound (now);
	std::vector<entry> result{ by_deadline.begin (), end };
	by_deadline.erase (by_deadline.begin (), end);
	return result;
}

void nano::vote_relay_client_index::clear ()
{
	entries.clear ();
}

std::size_t nano::vote_relay_client_index::outstanding (std::shared_ptr<nano::transport::channel> const & channel) const
{
	return entries.get<tag_channel> ().count (channel);
}

std::size_t nano::vote_relay_client_index::size () const
{
	return entries.size ();
}

bool nano::vote_relay_client_index::empty () const
{
	return entries.empty ();
}

/*
 * vote_relay_client
 */

nano::vote_relay_client::vote_relay_client (vote_relay_client_config const & config_a, nano::vote_processor & vote_processor_a, nano::network & network_a, nano::network_constants const & network_constants_a, nano::stats & stats_a, nano::logger & logger_a) :
	config{ config_a },
	vote_processor{ vote_processor_a },
	network{ network_a },
	network_constants{ network_constants_a },
	stats{ stats_a },
	logger{ logger_a }
{
}

void nano::vote_relay_client::stop ()
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	stopped = true;
	index.clear ();
}

std::deque<std::shared_ptr<nano::transport::channel>> nano::vote_relay_client::relays (std::size_t max_count) const
{
	// Capable peers with spare capacity for relay traffic, the network shuffles the list
	auto const channels = network.list (0, [] (auto const & channel) {
		return channel->get_flags ().test (nano::node_capabilities::vote_relay) && !channel->max (nano::transport::traffic_type::vote_relay);
	});

	std::deque<std::shared_ptr<nano::transport::channel>> result;
	nano::lock_guard<nano::mutex> guard{ mutex };
	for (auto const & channel : channels)
	{
		if (result.size () >= max_count)
		{
			break;
		}
		// A relay that has not answered its previous requests is left alone until it catches up
		if (index.outstanding (channel) < config.max_outstanding)
		{
			result.push_back (channel);
		}
	}
	return result;
}

bool nano::vote_relay_client::request (std::shared_ptr<nano::transport::channel> const & relay, std::deque<nano::account> const & reps, roots_hashes_t const & roots_hashes, bool include_non_final)
{
	release_assert (relay != nullptr);
	debug_assert (!reps.empty ()); // The relay does not serve requests for any representative
	debug_assert (reps.size () <= nano::messages::vote_relay_req::max_reps);
	debug_assert (!roots_hashes.empty ());
	debug_assert (roots_hashes.size () <= nano::messages::vote_relay_req::max_hashes);

	if (relay->max (nano::transport::traffic_type::vote_relay))
	{
		stats.inc (nano::stat::type::vote_relay_client, nano::stat::detail::channel_full);
		return false;
	}

	auto const now = std::chrono::steady_clock::now ();

	id_t id{ 0 };
	std::optional<nano::stat::detail> refused;
	std::vector<nano::vote_relay_client_index::entry> expired;
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		if (stopped)
		{
			return false;
		}

		// Expire lost requests first so they do not hold slots against the caps
		expired = index.evict (now);

		if (index.size () >= config.max_requests)
		{
			refused = nano::stat::detail::overfill;
		}
		else if (index.outstanding (relay) >= config.max_outstanding)
		{
			refused = nano::stat::detail::relay_full;
		}
		else
		{
			// Draw a fresh id until it is unused, a collision is practically impossible
			do
			{
				id = next_id ();
			} while (!index.insert ({ id, relay, now + config.request_timeout, roots_hashes.size () }));
		}
	}

	for (auto const & entry : expired)
	{
		stats.inc (nano::stat::type::vote_relay_client, nano::stat::detail::timeout);
		logger.debug (nano::log::type::vote_relay_client, "Request: {} timed out with {} votes received from relay: {}", entry.id, entry.votes, entry.channel);
	}

	if (refused)
	{
		stats.inc (nano::stat::type::vote_relay_client, *refused);
		return false;
	}

	// The wire message carries the reps as a vector, sized once from the list
	std::vector<nano::account> const reps_l (reps.begin (), reps.end ());
	nano::messages::vote_relay_req message{ network_constants, id, roots_hashes, reps_l, include_non_final };
	if (!relay->send (message, nano::transport::traffic_type::vote_relay))
	{
		{
			nano::lock_guard<nano::mutex> guard{ mutex };
			index.erase (id);
		}
		stats.inc (nano::stat::type::vote_relay_client, nano::stat::detail::request_failed);
		return false;
	}

	stats.inc (nano::stat::type::vote_relay_client, nano::stat::detail::request);
	logger.debug (nano::log::type::vote_relay_client, "Requesting votes from {} representatives for {} hashes via relay: {} (id: {}, include_non_final: {})",
	reps.size (),
	roots_hashes.size (),
	relay,
	id,
	include_non_final);

	return true;
}

bool nano::vote_relay_client::process (nano::messages::vote_relay_ack const & ack, std::shared_ptr<nano::transport::channel> const & channel)
{
	release_assert (channel != nullptr);

	std::optional<nano::vote_relay_client_index::entry> entry;
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		if (stopped)
		{
			return false;
		}
		entry = index.find (ack.id, channel);
		if (entry)
		{
			// An empty ack terminates the request, anything else accumulates
			if (ack.votes.empty ())
			{
				index.erase (ack.id);
			}
			else
			{
				index.received (ack.id, ack.votes.size ());
			}
		}
	}

	// Only acks for requests this node made are accepted, and only from the relay that was asked
	if (!entry)
	{
		stats.inc (nano::stat::type::vote_relay_client, nano::stat::detail::unsolicited);
		return false;
	}

	stats.inc (nano::stat::type::vote_relay_client, nano::stat::detail::reply);

	if (ack.votes.empty ())
	{
		stats.inc (nano::stat::type::vote_relay_client, nano::stat::detail::done);
		logger.debug (nano::log::type::vote_relay_client, "Request: {} completed with {} votes received from relay: {}", ack.id, entry->votes, channel);
		return true;
	}

	for (auto const & vote : ack.votes)
	{
		// Zero account votes are dropped as they are for confirm_ack
		if (vote->account.is_zero ())
		{
			stats.inc (nano::stat::type::vote_relay_client, nano::stat::detail::drop);
			continue;
		}
		// The relay source keeps the rep crawler from attributing the vote to the relay channel
		if (vote_processor.vote (vote, channel, nano::vote_source::relay))
		{
			stats.inc (nano::stat::type::vote_relay_client, nano::stat::detail::vote);
		}
		else
		{
			stats.inc (nano::stat::type::vote_relay_client, nano::stat::detail::queue_overflow);
		}
	}

	return true;
}

std::size_t nano::vote_relay_client::outstanding (std::shared_ptr<nano::transport::channel> const & channel) const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return index.outstanding (channel);
}

std::size_t nano::vote_relay_client::size () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return index.size ();
}

bool nano::vote_relay_client::empty () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return index.empty ();
}

nano::vote_relay_client::id_t nano::vote_relay_client::next_id ()
{
	return nano::random_pool::generate_word64 (1, std::numeric_limits<uint64_t>::max ());
}

nano::container_info nano::vote_relay_client::container_info () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };

	nano::container_info info;
	info.put ("requests", index.size ());
	return info;
}

/*
 * vote_relay_client_config
 */

nano::error nano::vote_relay_client_config::serialize (nano::tomlconfig & toml) const
{
	toml.put ("request_timeout", request_timeout.count (), "Time to wait for a relay to finish a request before considering it lost. \ntype:milliseconds");
	toml.put ("max_requests", max_requests, "Maximum number of relay requests waiting for their terminating ack. \ntype:uint64");
	toml.put ("max_outstanding", max_outstanding, "Maximum number of outstanding requests per relay before it stops receiving new ones. \ntype:uint64");

	return toml.get_error ();
}

nano::error nano::vote_relay_client_config::deserialize (nano::tomlconfig & toml)
{
	toml.get_duration ("request_timeout", request_timeout);
	toml.get ("max_requests", max_requests);
	toml.get ("max_outstanding", max_outstanding);

	return toml.get_error ();
}
