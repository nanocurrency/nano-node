#include "celerix/secure/ledger.hpp"

#include <celerix/lib/thread_roles.hpp>
#include <celerix/lib/utility.hpp>
#include <celerix/node/monitor.hpp>
#include <celerix/node/node.hpp>

celerix::monitor::monitor (celerix::monitor_config const & config_a, celerix::node & node_a) :
	config{ config_a },
	node{ node_a },
	logger{ node_a.logger }
{
}

celerix::monitor::~monitor ()
{
	debug_assert (!thread.joinable ());
}

void celerix::monitor::start ()
{
	if (!config.enable)
	{
		return;
	}

	thread = std::thread ([this] () {
		celerix::thread_role::set (celerix::thread_role::name::monitor);
		run ();
	});
}

void celerix::monitor::stop ()
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

void celerix::monitor::run ()
{
	std::unique_lock<celerix::mutex> lock{ mutex };
	while (!stopped)
	{
		run_one ();
		condition.wait_until (lock, std::chrono::steady_clock::now () + config.interval, [this] { return stopped; });
	}
}

void celerix::monitor::run_one ()
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
		logger.info (celerix::log::type::monitor, "Blocks confirmed: {} | total: {}",
		blocks_cemented,
		blocks_total);

		// Calculate the rates
		auto elapsed_seconds = std::chrono::duration_cast<std::chrono::seconds> (now - last_time).count ();
		auto blocks_confirmed_rate = static_cast<double> (blocks_cemented - last_blocks_cemented) / elapsed_seconds;
		auto blocks_checked_rate = static_cast<double> (blocks_total - last_blocks_total) / elapsed_seconds;

		logger.info (celerix::log::type::monitor, "Blocks rate (average over last {}s): confirmed {:.2f}/s | total {:.2f}/s",
		elapsed_seconds,
		blocks_confirmed_rate,
		blocks_checked_rate);

		logger.info (celerix::log::type::monitor, "Peers: {} (realtime: {} | bootstrap: {} | inbound connections: {} | outbound connections: {})",
		node.network.size (),
		node.tcp_listener.realtime_count (),
		node.tcp_listener.bootstrap_count (),
		node.tcp_listener.connection_count (celerix::transport::tcp_listener::connection_type::inbound),
		node.tcp_listener.connection_count (celerix::transport::tcp_listener::connection_type::outbound));

		logger.info (celerix::log::type::monitor, "Quorum: {} (stake peered: {} | stake online: {})",
		celerix::uint128_union{ node.online_reps.delta () }.format_balance (celerix_ratio, 1, true),
		celerix::uint128_union{ node.rep_crawler.total_weight () }.format_balance (celerix_ratio, 1, true),
		celerix::uint128_union{ node.online_reps.online () }.format_balance (celerix_ratio, 1, true));

		logger.info (celerix::log::type::monitor, "Elections active: {} (priority: {} | hinted: {} | optimistic: {})",
		node.active.size (),
		node.active.size (celerix::election_behavior::priority),
		node.active.size (celerix::election_behavior::hinted),
		node.active.size (celerix::election_behavior::optimistic));
	}

	last_time = now;
	last_blocks_cemented = blocks_cemented;
	last_blocks_total = blocks_total;
}

/*
 * monitor_config
 */

celerix::error celerix::monitor_config::serialize (celerix::tomlconfig & toml) const
{
	toml.put ("enable", enable, "Enable or disable periodic node status logging\ntype:bool");
	toml.put ("interval", interval.count (), "Interval between status logs\ntype:seconds");

	return toml.get_error ();
}

celerix::error celerix::monitor_config::deserialize (celerix::tomlconfig & toml)
{
	toml.get ("enable", enable);
	auto interval_l = interval.count ();
	toml.get ("interval", interval_l);
	interval = std::chrono::seconds{ interval_l };

	return toml.get_error ();
}