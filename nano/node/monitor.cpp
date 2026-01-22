#include <nano/lib/thread_roles.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/monitor.hpp>
#include <nano/node/node.hpp>
#include <nano/secure/ledger.hpp>

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
	if (!config.enable)
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
	auto const now = std::chrono::steady_clock::now ();

	auto blocks_cemented = node.ledger.cemented_count ();
	auto blocks_total = node.ledger.block_count ();

	// TODO: Maybe emphasize somehow that confirmed doesn't need to be equal to total; backlog is OK
	logger.info (nano::log::type::monitor, "Blocks confirmed: {} | total: {} (backlog: {})",
	blocks_cemented,
	blocks_total,
	blocks_total - blocks_cemented);

	// Wait for node to warm up before logging rates
	if (last_time != std::chrono::steady_clock::time_point{})
	{
		auto elapsed_seconds = std::chrono::duration_cast<std::chrono::seconds> (now - last_time).count ();
		debug_assert (elapsed_seconds > 0);

		// Cast to signed type to correctly handle negative differences (e.g., from rollbacks)
		auto cemented_diff = static_cast<std::int64_t> (blocks_cemented) - static_cast<std::int64_t> (last_blocks_cemented);
		auto total_diff = static_cast<std::int64_t> (blocks_total) - static_cast<std::int64_t> (last_blocks_total);

		// Calculate the rates
		auto blocks_confirmed_rate = static_cast<double> (cemented_diff) / elapsed_seconds;
		auto blocks_checked_rate = static_cast<double> (total_diff) / elapsed_seconds;

		logger.info (nano::log::type::monitor, "Blocks rate (avg over {}s): confirmed {:.2f}/s | total {:.2f}/s",
		elapsed_seconds,
		blocks_confirmed_rate,
		blocks_checked_rate);
	}

	logger.info (nano::log::type::monitor, "Peers: {} (realtime: {} | bootstrap: {}) (inbound: {} | outbound: {})",
	node.network.size (),
	node.tcp_listener.realtime_count (),
	node.tcp_listener.bootstrap_count (),
	node.tcp_listener.connection_count (nano::transport::tcp_listener::connection_type::inbound),
	node.tcp_listener.connection_count (nano::transport::tcp_listener::connection_type::outbound));

	auto const quorum = node.online_reps.delta ();
	auto const stake_online = node.online_reps.online ();
	auto const stake_peered = node.rep_crawler.total_weight ();

	logger.info (nano::log::type::monitor, "Quorum: {} (stake peered: {} | stake online: {})",
	nano::uint128_union{ quorum }.format_balance (nano_ratio, 1, true),
	nano::uint128_union{ stake_peered }.format_balance (nano_ratio, 1, true),
	nano::uint128_union{ stake_online }.format_balance (nano_ratio, 1, true));

	logger.info (nano::log::type::monitor, "Elections active: {} (priority: {} | hinted: {} | optimistic: {}) of which stale: {}",
	node.active.size (),
	node.active.size (nano::election_behavior::priority),
	node.active.size (nano::election_behavior::hinted),
	node.active.size (nano::election_behavior::optimistic),
	node.active.stale_count ());

	bool const sufficient_stake = stake_peered >= quorum;

	if (!sufficient_stake && node.warmed_up ())
	{
		logger.warn (nano::log::type::monitor, "Peered stake ({}) is below quorum threshold ({}). The node may not be able to confirm transactions. This is usually caused by NAT, firewall rules, or internet connectivity issues.",
		nano::uint128_union{ stake_peered }.format_balance (nano_ratio, 1, true),
		nano::uint128_union{ quorum }.format_balance (nano_ratio, 1, true));
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
	toml.put ("enable", enable, "Enable or disable periodic node status logging\ntype:bool");
	toml.put ("interval", interval.count (), "Interval between status logs\ntype:seconds");

	return toml.get_error ();
}

nano::error nano::monitor_config::deserialize (nano::tomlconfig & toml)
{
	toml.get ("enable", enable);
	toml.get_duration ("interval", interval);

	return toml.get_error ();
}