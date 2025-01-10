#include <celerix/lib/thread_roles.hpp>
#include <celerix/node/network.hpp>
#include <celerix/node/peer_history.hpp>
#include <celerix/node/transport/channel.hpp>
#include <celerix/store/component.hpp>
#include <celerix/store/peer.hpp>

celerix::peer_history::peer_history (celerix::peer_history_config const & config_a, celerix::store::component & store_a, celerix::network & network_a, celerix::logger & logger_a, celerix::stats & stats_a) :
	config{ config_a },
	store{ store_a },
	network{ network_a },
	logger{ logger_a },
	stats{ stats_a }
{
}

celerix::peer_history::~peer_history ()
{
	debug_assert (!thread.joinable ());
}

void celerix::peer_history::start ()
{
	debug_assert (!thread.joinable ());

	thread = std::thread ([this] {
		celerix::thread_role::set (celerix::thread_role::name::peer_history);
		run ();
	});
}

void celerix::peer_history::stop ()
{
	{
		celerix::lock_guard<celerix::mutex> guard{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	if (thread.joinable ())
	{
		thread.join ();
	}
}

bool celerix::peer_history::exists (celerix::endpoint const & endpoint) const
{
	auto transaction = store.tx_begin_read ();
	return store.peer.exists (transaction, endpoint);
}

size_t celerix::peer_history::size () const
{
	auto transaction = store.tx_begin_read ();
	return store.peer.count (transaction);
}

void celerix::peer_history::trigger ()
{
	condition.notify_all ();
}

void celerix::peer_history::run ()
{
	celerix::unique_lock<celerix::mutex> lock{ mutex };
	while (!stopped)
	{
		condition.wait_for (lock, config.check_interval, [this] { return stopped.load (); });
		if (!stopped)
		{
			stats.inc (celerix::stat::type::peer_history, celerix::stat::detail::loop);

			lock.unlock ();

			run_one ();

			lock.lock ();
		}
	}
}

void celerix::peer_history::run_one ()
{
	auto live_peers = network.list ();
	auto transaction = store.tx_begin_write ();

	// Add or update live peers
	for (auto const & peer : live_peers)
	{
		auto const endpoint = peer->get_peering_endpoint ();
		bool const exists = store.peer.exists (transaction, endpoint);
		store.peer.put (transaction, endpoint, celerix::milliseconds_since_epoch ());
		if (!exists)
		{
			stats.inc (celerix::stat::type::peer_history, celerix::stat::detail::inserted);
			logger.debug (celerix::log::type::peer_history, "Saved new peer: {}", fmt::streamed (endpoint));
		}
		else
		{
			stats.inc (celerix::stat::type::peer_history, celerix::stat::detail::updated);
		}
	}

	// Erase old peers
	auto const now = std::chrono::system_clock::now ();
	auto const cutoff = now - config.erase_cutoff;

	std::deque<celerix::store::peer::iterator::value_type> to_remove;

	for (auto it = store.peer.begin (transaction); it != store.peer.end (transaction); ++it)
	{
		auto const [endpoint, timestamp_millis] = *it;
		auto timestamp = celerix::from_milliseconds_since_epoch (timestamp_millis);
		if (timestamp > now || timestamp < cutoff)
		{
			to_remove.push_back (*it);

			stats.inc (celerix::stat::type::peer_history, celerix::stat::detail::erased);
			logger.debug (celerix::log::type::peer_history, "Erased peer: {} (not seen for {}s)",
			fmt::streamed (endpoint.endpoint ()),
			celerix::log::seconds_delta (timestamp));
		}
	}

	// Remove entries after iterating to avoid iterator invalidation
	for (auto const & entry : to_remove)
	{
		store.peer.del (transaction, entry.first);
	}
}

std::vector<celerix::endpoint> celerix::peer_history::peers () const
{
	auto transaction = store.tx_begin_read ();
	std::vector<celerix::endpoint> peers;
	for (auto it = store.peer.begin (transaction); it != store.peer.end (transaction); ++it)
	{
		auto const [endpoint, timestamp_millis] = *it;
		peers.push_back (endpoint.endpoint ());
	}
	return peers;
}

/*
 * peer_history_config
 */

celerix::peer_history_config::peer_history_config (celerix::network_constants const & network)
{
	if (network.is_dev_network ())
	{
		check_interval = 1s;
		erase_cutoff = 10s;
	}
}

celerix::error celerix::peer_history_config::serialize (celerix::tomlconfig & toml) const
{
	// TODO: Serialization / deserialization
	return toml.get_error ();
}

celerix::error celerix::peer_history_config::deserialize (celerix::tomlconfig & toml)
{
	// TODO: Serialization / deserialization
	return toml.get_error ();
}
