#include <nano/lib/thread_roles.hpp>
#include <nano/node/network.hpp>
#include <nano/node/peer_cache.hpp>
#include <nano/node/transport/channel.hpp>
#include <nano/store/component.hpp>
#include <nano/store/peer.hpp>

nano::peer_cache::peer_cache (nano::peer_cache_config const & config_a, nano::store::component & store_a, nano::network & network_a, nano::logger & logger_a, nano::stats & stats_a) :
	config{ config_a },
	store{ store_a },
	network{ network_a },
	logger{ logger_a },
	stats{ stats_a }
{
}

nano::peer_cache::~peer_cache ()
{
	debug_assert (!thread.joinable ());
}

void nano::peer_cache::start ()
{
	debug_assert (!thread.joinable ());

	thread = std::thread ([this] {
		nano::thread_role::set (nano::thread_role::name::peer_cache);
		run ();
	});
}

void nano::peer_cache::stop ()
{
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	if (thread.joinable ())
	{
		thread.join ();
	}
}

bool nano::peer_cache::exists (nano::endpoint const & endpoint) const
{
	auto transaction = store.tx_begin_read ();
	return store.peer.exists (transaction, endpoint);
}

size_t nano::peer_cache::size () const
{
	auto transaction = store.tx_begin_read ();
	return store.peer.count (transaction);
}

void nano::peer_cache::trigger ()
{
	condition.notify_all ();
}

void nano::peer_cache::run ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		condition.wait_for (lock, config.check_interval, [this] { return stopped.load (); });
		if (!stopped)
		{
			stats.inc (nano::stat::type::peer_cache, nano::stat::detail::loop);

			lock.unlock ();

			run_one ();

			lock.lock ();
		}
	}
}

void nano::peer_cache::run_one ()
{
	auto live_peers = network.list ();
	auto transaction = store.tx_begin_write ({ tables::peers });

	// Add or update live peers
	for (auto const & peer : live_peers)
	{
		auto const endpoint = peer->get_endpoint ();
		bool const exists = store.peer.exists (transaction, endpoint);
		store.peer.put (transaction, endpoint, nano::milliseconds_since_epoch ());
		if (!exists)
		{
			stats.inc (nano::stat::type::peer_cache, nano::stat::detail::inserted);
			logger.debug (nano::log::type::peer_cache, "Cached new peer: {}", fmt::streamed (endpoint));
		}
		else
		{
			stats.inc (nano::stat::type::peer_cache, nano::stat::detail::updated);
		}
	}

	// Erase old peers
	auto const now = std::chrono::system_clock::now ();
	auto const cutoff = now - config.erase_cutoff;

	for (auto it = store.peer.begin (transaction); it != store.peer.end (); ++it)
	{
		auto const [endpoint, timestamp_millis] = *it;
		auto timestamp = nano::from_milliseconds_since_epoch (timestamp_millis);
		if (timestamp > now || timestamp < cutoff)
		{
			store.peer.del (transaction, endpoint);

			stats.inc (nano::stat::type::peer_cache, nano::stat::detail::erased);
			logger.debug (nano::log::type::peer_cache, "Erased peer: {} (not seen for {}s)",
			fmt::streamed (endpoint.endpoint ()),
			nano::log::seconds_delta (timestamp));
		}
	}
}

std::vector<nano::endpoint> nano::peer_cache::cached_peers () const
{
	auto transaction = store.tx_begin_read ();
	std::vector<nano::endpoint> peers;
	for (auto it = store.peer.begin (transaction); it != store.peer.end (); ++it)
	{
		auto const [endpoint, timestamp_millis] = *it;
		peers.push_back (endpoint.endpoint ());
	}
	return peers;
}

/*
 * peer_cache_config
 */

nano::peer_cache_config::peer_cache_config (nano::network_constants const & network)
{
	if (network.is_dev_network ())
	{
		check_interval = 1s;
		erase_cutoff = 3s;
	}
}

nano::error nano::peer_cache_config::serialize (nano::tomlconfig & toml) const
{
	return toml.get_error ();
}

nano::error nano::peer_cache_config::deserialize (nano::tomlconfig & toml)
{
	return toml.get_error ();
}
