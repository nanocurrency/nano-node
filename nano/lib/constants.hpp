#pragma once

#include <nano/lib/config.hpp>
#include <nano/lib/fwd.hpp>

#include <chrono>
#include <string_view>

namespace nano
{
/**
 * Network variants with different genesis blocks and network parameters
 */
enum class networks : uint16_t
{
	invalid = 0x0,
	// Low work parameters, publicly known genesis key, dev IP ports
	nano_dev_network = 0x5241, // 'R', 'A'
	// Normal work parameters, secret beta genesis key, beta IP ports
	nano_beta_network = 0x5242, // 'R', 'B'
	// Normal work parameters, secret live key, live IP ports
	nano_live_network = 0x5243, // 'R', 'C'
	// Normal work parameters, secret test genesis key, test IP ports
	nano_test_network = 0x5258, // 'R', 'X'
};

std::string_view to_string (nano::networks);

class work_thresholds
{
public:
	uint64_t const epoch_1;
	uint64_t const epoch_2;
	uint64_t const epoch_2_receive;

	// Automatically calculated. The base threshold is the maximum of all thresholds and is used for all work multiplier calculations
	uint64_t const base;

	// Automatically calculated. The entry threshold is the minimum of all thresholds and defines the required work to enter the node, but does not guarantee a block is processed
	uint64_t const entry;

	constexpr work_thresholds (uint64_t epoch_1_a, uint64_t epoch_2_a, uint64_t epoch_2_receive_a) :
		epoch_1 (epoch_1_a), epoch_2 (epoch_2_a), epoch_2_receive (epoch_2_receive_a),
		base (std::max ({ epoch_1, epoch_2, epoch_2_receive })),
		entry (std::min ({ epoch_1, epoch_2, epoch_2_receive }))
	{
	}
	work_thresholds () = delete;
	work_thresholds operator= (nano::work_thresholds const & other_a)
	{
		return other_a;
	}

	uint64_t threshold_entry (nano::work_version const, nano::block_type const) const;
	uint64_t threshold (nano::block_details const &) const;
	// Ledger threshold
	uint64_t threshold (nano::work_version const, nano::block_details const) const;
	uint64_t threshold_base (nano::work_version const) const;
	uint64_t value (nano::root const & root_a, uint64_t work_a) const;
	double normalized_multiplier (double const, uint64_t const) const;
	double denormalized_multiplier (double const, uint64_t const) const;
	uint64_t difficulty (nano::work_version const, nano::root const &, uint64_t const) const;
	uint64_t difficulty (nano::block const & block_a) const;
	bool validate_entry (nano::work_version const, nano::root const &, uint64_t const) const;
	bool validate_entry (nano::block const &) const;

	/** Network work thresholds. Define these inline as constexpr when moving to cpp17. */
	static nano::work_thresholds const publish_full;
	static nano::work_thresholds const publish_beta;
	static nano::work_thresholds const publish_dev;
	static nano::work_thresholds const publish_test;
};

class network_constants
{
	static constexpr std::chrono::seconds default_cleanup_period = std::chrono::seconds (60);

public:
	network_constants (nano::work_thresholds & work_, nano::networks network_a) :
		current_network (network_a),
		work (work_),
		principal_weight_factor (1000), // 0.1% A representative is classified as principal based on its weight and this factor
		default_node_port (44000),
		default_rpc_port (45000),
		default_ipc_port (46000),
		default_websocket_port (47000),
		aec_loop_interval (300ms), // Update AEC ~3 times per second
		cleanup_period (default_cleanup_period),
		merge_period (std::chrono::milliseconds (250)),
		keepalive_period (std::chrono::seconds (15)),
		idle_timeout (default_cleanup_period * 2),
		silent_connection_tolerance_time (std::chrono::seconds (120)),
		syn_cookie_cutoff (std::chrono::seconds (5)),
		bootstrap_interval (std::chrono::seconds (15 * 60)),
		ipv6_subnetwork_prefix_for_limiting (64), // Equivalent to network prefix /64.
		peer_dump_interval (std::chrono::seconds (5 * 60)),
		vote_broadcast_interval (15 * 1000),
		block_broadcast_interval (150 * 1000)
	{
		if (is_live_network ())
		{
			default_node_port = 7075;
			default_rpc_port = 7076;
			default_ipc_port = 7077;
			default_websocket_port = 7078;
		}
		else if (is_beta_network ())
		{
			default_node_port = 54000;
			default_rpc_port = 55000;
			default_ipc_port = 56000;
			default_websocket_port = 57000;
		}
		else if (is_test_network ())
		{
			default_node_port = test_node_port ();
			default_rpc_port = test_rpc_port ();
			default_ipc_port = test_ipc_port ();
			default_websocket_port = test_websocket_port ();
		}
		else if (is_dev_network ())
		{
			aec_loop_interval = 20ms;
			cleanup_period = std::chrono::seconds (1);
			merge_period = std::chrono::milliseconds (10);
			keepalive_period = std::chrono::seconds (1);
			idle_timeout = cleanup_period * 15;
			peer_dump_interval = std::chrono::seconds (1);
			vote_broadcast_interval = 500ms;
			block_broadcast_interval = 500ms;
			telemetry_request_cooldown = 500ms;
			telemetry_cache_cutoff = 2000ms;
			telemetry_request_interval = 500ms;
			telemetry_broadcast_interval = 500ms;
			optimistic_activation_delay = 2s;
			rep_crawler_normal_interval = 500ms;
			rep_crawler_warmup_interval = 500ms;
		}
	}

	/** The network this param object represents. This may differ from the global active network; this is needed for certain --debug... commands */
	nano::networks current_network{ nano::network_constants::active_network };
	nano::work_thresholds & work;

	unsigned principal_weight_factor;
	uint16_t default_node_port;
	uint16_t default_rpc_port;
	uint16_t default_ipc_port;
	uint16_t default_websocket_port;
	std::chrono::milliseconds aec_loop_interval;

	std::chrono::seconds cleanup_period;
	std::chrono::milliseconds cleanup_period_half () const
	{
		return std::chrono::duration_cast<std::chrono::milliseconds> (cleanup_period) / 2;
	}
	std::chrono::seconds cleanup_cutoff () const
	{
		return cleanup_period * 5;
	}
	/** How often to connect to other peers */
	std::chrono::milliseconds merge_period;
	/** How often to send keepalive messages */
	std::chrono::seconds keepalive_period;
	/** Default maximum idle time for a socket before it's automatically closed */
	std::chrono::seconds idle_timeout;
	std::chrono::seconds silent_connection_tolerance_time;
	std::chrono::seconds syn_cookie_cutoff;
	std::chrono::seconds bootstrap_interval;
	size_t ipv6_subnetwork_prefix_for_limiting;
	std::chrono::seconds peer_dump_interval;

	/** Time to wait before rebroadcasts for active elections */
	std::chrono::milliseconds vote_broadcast_interval;
	std::chrono::milliseconds block_broadcast_interval;

	/** We do not reply to telemetry requests made within cooldown period */
	std::chrono::milliseconds telemetry_request_cooldown{ 1000 * 15 };
	/** How often to request telemetry from peers */
	std::chrono::milliseconds telemetry_request_interval{ 1000 * 60 };
	/** How often to broadcast telemetry to peers */
	std::chrono::milliseconds telemetry_broadcast_interval{ 1000 * 60 };
	/** Telemetry data older than this value is considered stale */
	std::chrono::milliseconds telemetry_cache_cutoff{ 1000 * 130 }; // 2 * `telemetry_broadcast_interval` + some margin

	/** How much to delay activation of optimistic elections to avoid interfering with election scheduler */
	std::chrono::seconds optimistic_activation_delay{ 30 };

	std::chrono::milliseconds rep_crawler_normal_interval{ 1000 * 7 };
	std::chrono::milliseconds rep_crawler_warmup_interval{ 1000 * 3 };

	/** Returns the network this object contains values for */
	nano::networks network () const
	{
		return current_network;
	}

	/**
	 * Optionally called on startup to override the global active network.
	 * If not called, the compile-time option will be used.
	 * @param network_a The new active network
	 */
	static void set_active_network (nano::networks network_a)
	{
		active_network = network_a;
	}

	static nano::networks get_active_network ()
	{
		return active_network;
	}

	/**
	 * Optionally called on startup to override the global active network.
	 * If not called, the compile-time option will be used.
	 * @param network_a The new active network. Valid values are "live", "beta" and "dev"
	 */
	static bool set_active_network (std::string network_a)
	{
		auto error{ false };
		if (network_a == "live")
		{
			active_network = nano::networks::nano_live_network;
		}
		else if (network_a == "beta")
		{
			active_network = nano::networks::nano_beta_network;
		}
		else if (network_a == "dev")
		{
			active_network = nano::networks::nano_dev_network;
		}
		else if (network_a == "test")
		{
			active_network = nano::networks::nano_test_network;
		}
		else
		{
			error = true;
		}
		return error;
	}

	std::string_view get_current_network_as_string () const
	{
		switch (current_network)
		{
			case nano::networks::nano_live_network:
				return "live";
			case nano::networks::nano_beta_network:
				return "beta";
			case nano::networks::nano_dev_network:
				return "dev";
			case nano::networks::nano_test_network:
				return "test";
			case networks::invalid:
				break;
		}
		release_assert (false, "invalid network");
	}

	bool is_live_network () const
	{
		return current_network == nano::networks::nano_live_network;
	}
	bool is_beta_network () const
	{
		return current_network == nano::networks::nano_beta_network;
	}
	bool is_dev_network () const
	{
		return current_network == nano::networks::nano_dev_network;
	}
	bool is_test_network () const
	{
		return current_network == nano::networks::nano_test_network;
	}

	/** Initial value is ACTIVE_NETWORK compile flag, but can be overridden by a CLI flag */
	static nano::networks active_network;

	/** Current protocol version */
	uint8_t const protocol_version = 0x15;
	/** Minimum accepted protocol version */
	uint8_t const protocol_version_min = 0x14;

	/** Minimum accepted protocol version used when bootstrapping */
	uint8_t const bootstrap_protocol_version_min = 0x14;
};
}