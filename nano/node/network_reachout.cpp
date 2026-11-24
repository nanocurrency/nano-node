#include <nano/lib/logging.hpp>
#include <nano/lib/network_formatting.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/lib/threading.hpp>
#include <nano/node/network.hpp>
#include <nano/node/network_reachout.hpp>
#include <nano/node/node.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/peer_history.hpp>
#include <nano/node/transport/formatting.hpp>

using namespace std::chrono_literals;

namespace
{
auto constexpr preconfigured_log_interval = 1h;
}

nano::network_reachout::network_reachout (nano::node & node_a) :
	node{ node_a },
	resolver{ node.io_ctx }
{
}

nano::network_reachout::~network_reachout ()
{
	// All threads must be stopped before this destructor
	debug_assert (!live_thread.joinable ());
	debug_assert (!cached_thread.joinable ());
	debug_assert (!preconfigured_thread.joinable ());
}

void nano::network_reachout::start ()
{
	auto const & config = *node.config.network;

	if (!node.flags.disable_reachout && config.peer_reachout.count () > 0)
	{
		live_thread = std::thread ([this] () {
			nano::thread_role::set (nano::thread_role::name::network_reachout);
			run_live ();
		});
	}
	else
	{
		node.logger.warn (nano::log::type::network, "Peer reachout is disabled");
	}

	if (!node.flags.disable_reachout && config.cached_peer_reachout.count () > 0)
	{
		cached_thread = std::thread ([this] () {
			nano::thread_role::set (nano::thread_role::name::network_reachout);
			run_cached ();
		});
	}
	else
	{
		node.logger.warn (nano::log::type::network, "Cached peer reachout is disabled");
	}

	if (!node.flags.disable_reachout_preconfigured)
	{
		preconfigured_thread = std::thread ([this] () {
			nano::thread_role::set (nano::thread_role::name::network_reachout);
			run_preconfigured ();
		});
	}
}

void nano::network_reachout::stop ()
{
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();

	resolver.cancel ();

	join_or_pass (live_thread);
	join_or_pass (cached_thread);
	join_or_pass (preconfigured_thread);
}

void nano::network_reachout::run_live ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		condition.wait_for (lock, node.network_params.network.merge_period, [this] { return stopped.load (); });
		if (stopped)
		{
			return;
		}
		lock.unlock ();

		node.stats.inc (nano::stat::type::network, nano::stat::detail::loop_reachout);

		auto keepalive = node.network.tcp_channels.sample_keepalive ();
		if (keepalive)
		{
			for (auto const & peer : keepalive->peers)
			{
				if (stopped)
				{
					return;
				}

				node.stats.inc (nano::stat::type::network, nano::stat::detail::reachout_live);

				node.network.merge_peer (peer);

				// Throttle reachout attempts
				std::this_thread::sleep_for (node.network_params.network.merge_period);
			}
		}

		lock.lock ();
	}
}

void nano::network_reachout::run_cached ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		condition.wait_for (lock, node.network_params.network.merge_period, [this] { return stopped.load (); });
		if (stopped)
		{
			return;
		}
		lock.unlock ();

		node.stats.inc (nano::stat::type::network, nano::stat::detail::loop_reachout_cached);

		auto cached_peers = node.peer_history.peers ();
		for (auto const & peer : cached_peers)
		{
			if (stopped)
			{
				return;
			}

			node.stats.inc (nano::stat::type::network, nano::stat::detail::reachout_cached);

			node.network.merge_peer (peer);

			// Throttle reachout attempts
			std::this_thread::sleep_for (node.network_params.network.merge_period);
		}

		lock.lock ();
	}
}

void nano::network_reachout::run_preconfigured ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		condition.wait_for (lock, 1s, [this] { return stopped.load () || preconfigured_triggered; });
		if (stopped)
		{
			return;
		}
		preconfigured_triggered = false;
		lock.unlock ();

		node.stats.inc (nano::stat::type::network, nano::stat::detail::loop_reachout_preconfigured);

		auto const target_interval = node.network.empty ()
		? node.network_params.network.reachout_preconfigured_warmup_period
		: node.network_params.network.reachout_preconfigured_period;

		if (preconfigured_interval.elapse (target_interval))
		{
			reachout_preconfigured ();
		}

		lock.lock ();
	}
}

void nano::network_reachout::trigger ()
{
	node.stats.inc (nano::stat::type::network, nano::stat::detail::trigger_reachout);
	preconfigured_interval.reset ();
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		preconfigured_triggered = true;
	}
	condition.notify_all ();
}

void nano::network_reachout::reachout (std::string const & address_a, uint16_t port_a, nano::transport::connect_callback callback_a)
{
	auto node_l (node.shared_from_this ());
	resolver.async_resolve (address_a, std::to_string (port_a), [this, node_l, address_a, port_a, callback = std::move (callback_a)] (boost::system::error_code const & ec, boost::asio::ip::tcp::resolver::results_type results) {
		if (!ec)
		{
			for (auto const & i : results)
			{
				auto endpoint (nano::transport::map_endpoint_to_v6 (i.endpoint ()));
				auto channel (node.network.find_channel (endpoint));
				if (!channel)
				{
					node.network.tcp_channels.start_tcp (endpoint, callback);
				}
				else
				{
					node.network.send_keepalive (channel);
				}
			}
		}
		else
		{
			node.logger.error (nano::log::type::network, "Error resolving address for reachout: {}:{} ({})", address_a, port_a, ec.message ());
		}
	});
}

void nano::network_reachout::reachout_preconfigured ()
{
	if (node.config.preconfigured_peers.empty ())
	{
		return;
	}

	node.stats.inc (nano::stat::type::network, nano::stat::detail::reachout_preconfigured);

	for (auto const & peer : node.config.preconfigured_peers)
	{
		reachout (peer, node.network_params.network.default_node_port, [node_l = node.shared_from_this (), peer] (nano::tcp_endpoint const & endpoint, std::error_code ec) {
			auto should_log = [node_l, &peer] (bool success) {
				auto logs = node_l->network_reachout.preconfigured_logs.lock ();
				return (*logs)[peer].should_log (success);
			};

			if (ec)
			{
				// Log connection failures to explicitly configured peers, so misconfiguration is visible
				if (should_log (false))
				{
					node_l->logger.warn (nano::log::type::network, "Failed to connect to preconfigured peer '{}' ({}): {}", peer, endpoint, ec.message ());
				}
			}
			else
			{
				if (should_log (true))
				{
					node_l->logger.info (nano::log::type::network, "Connected to preconfigured peer '{}' ({})", peer, endpoint);
				}
			}
		});
	}
}

bool nano::network_reachout::preconfigured_log::should_log (bool success)
{
	auto const now = std::chrono::steady_clock::now ();

	auto & last_log = success ? last_success : last_failure;
	if (now - last_log >= preconfigured_log_interval)
	{
		last_log = now;
		return true;
	}
	return false;
}
