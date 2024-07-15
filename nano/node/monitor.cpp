#include "nano/secure/ledger.hpp"

#include <nano/lib/thread_roles.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/monitor.hpp>
#include <nano/node/node.hpp>

nano::monitor::monitor (nano::monitor_config const & config_a, nano::node & node_a) :
	config{ config_a },
	node{ node_a },
	logger{ node_a.logger }
{
}

nano::monitor::~monitor ()
{
	debug_assert (!thread.joinable ());
}

void nano::monitor::start ()
{
	if (!config.enabled)
	{
		return;
	}

	thread = std::thread ([this] () {
		nano::thread_role::set (nano::thread_role::name::monitor);
		run ();
	});
}

void nano::monitor::stop ()
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

void nano::monitor::run ()
{
	std::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		run_one ();
		condition.wait_until (lock, std::chrono::steady_clock::now () + config.interval, [this] { return stopped; });
	}
}

void nano::monitor::run_one ()
{
	// Node status:
	// - blocks (confirmed, total)
	// - blocks rate (over last 5m, peak over last 5m)
	// - peers
	// - stake (online, peered, trended, quorum needed)
	// - elections active (normal, hinted, optimistic)
	// - election stats over last 5m (confirmed, dropped)

	auto const now = std::chrono::steady_clock::now ();
	auto blocks_cemented = node.ledger.cemented_count ();
	auto blocks_total = node.ledger.block_count ();

	// Wait for node to warm up before logging
	if (last_time != std::chrono::steady_clock::time_point{})
	{
		// TODO: Maybe emphasize somehow that confirmed doesn't need to be equal to total; backlog is OK
		logger.info (nano::log::type::monitor, "Blocks confirmed: {} | total: {}",
		blocks_cemented,
		blocks_total);

		// Calculate the rates
		auto elapsed_seconds = std::chrono::duration_cast<std::chrono::seconds> (now - last_time).count ();
		auto blocks_confirmed_rate = static_cast<double> (blocks_cemented - last_blocks_cemented) / elapsed_seconds;
		auto blocks_checked_rate = static_cast<double> (blocks_total - last_blocks_total) / elapsed_seconds;

		logger.info (nano::log::type::monitor, "Blocks rate (average over last {}s): confirmed {:.2f}/s | total {:.2f}/s",
		elapsed_seconds,
		blocks_confirmed_rate,
		blocks_checked_rate);

		logger.info (nano::log::type::monitor, "Peers: {} (realtime: {} | bootstrap: {} | inbound connections: {} | outbound connections: {})",
		node.network.size (),
		node.tcp_listener.realtime_count (),
		node.tcp_listener.bootstrap_count (),
		node.tcp_listener.connection_count (nano::transport::tcp_listener::connection_type::inbound),
		node.tcp_listener.connection_count (nano::transport::tcp_listener::connection_type::outbound));

		logger.info (nano::log::type::monitor, "Quorum: {} (stake peered: {} | stake online: {})",
		nano::uint128_union{ node.online_reps.delta () }.format_balance (Mxrb_ratio, 1, true),
		nano::uint128_union{ node.rep_crawler.total_weight () }.format_balance (Mxrb_ratio, 1, true),
		nano::uint128_union{ node.online_reps.online () }.format_balance (Mxrb_ratio, 1, true));

		logger.info (nano::log::type::monitor, "Elections active: {} (priority: {} | hinted: {} | optimistic: {})",
		node.active.size (),
		node.active.size (nano::election_behavior::priority),
		node.active.size (nano::election_behavior::hinted),
		node.active.size (nano::election_behavior::optimistic));
	}

	last_time = now;
	last_blocks_cemented = blocks_cemented;
	last_blocks_total = blocks_total;
}

/*
 * monitor_config
 */

nano::error nano::monitor_config::serialize (nano::tomlconfig & toml) const
{
	toml.put ("enable", enabled, "Enable or disable periodic node status logging\ntype:bool");
	toml.put ("interval", interval.count (), "Interval between status logs\ntype:seconds");

	return toml.get_error ();
}

nano::error nano::monitor_config::deserialize (nano::tomlconfig & toml)
{
	toml.get ("enable", enabled);
	auto interval_l = interval.count ();
	toml.get ("interval", interval_l);
	interval = std::chrono::seconds{ interval_l };

	return toml.get_error ();
}