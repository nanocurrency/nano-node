#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include <magic_enum.hpp>

namespace nano::log
{
enum class level
{
	trace,
	debug,
	info,
	warn,
	error,
	critical,
	off,
};

enum class type
{
	all = 0, // reserved

	generic,
	test,
	system,
	init,
	config,
	stats,
	logging,
	node,
	node_wrapper,
	daemon,
	daemon_rpc,
	daemon_wallet,
	wallet,
	qt,
	rpc,
	rpc_connection,
	rpc_callbacks,
	rpc_request,
	ipc,
	ipc_server,
	websocket,
	tls,
	active_elections,
	election,
	blockprocessor,
	network,
	message,
	channel,
	channel_sent,
	socket,
	socket_server,
	tcp,
	tcp_server,
	tcp_listener,
	tcp_channels,
	prunning,
	conf_processor_bounded,
	conf_processor_unbounded,
	distributed_work,
	epoch_upgrader,
	opencl_work,
	upnp,
	rep_crawler,
	lmdb,
	rocksdb,
	txn_tracker,
	gap_cache,
	vote_processor,
	election_scheduler,
	vote_generator,
	rep_tiers,
	syn_cookies,
	thread_runner,
	signal_manager,
	peer_history,
	message_processor,
	online_reps,
	local_block_broadcaster,

	// bootstrap
	bulk_pull_client,
	bulk_pull_server,
	bulk_pull_account_client,
	bulk_pull_account_server,
	bulk_push_client,
	bulk_push_server,
	frontier_req_client,
	frontier_req_server,
	bootstrap,
	bootstrap_lazy,
	bootstrap_legacy,

	_last // Must be the last enum
};

enum class detail
{
	all = 0, // reserved

	test,

	// node
	process_confirmed,

	// active_elections
	active_started,
	active_stopped,
	active_cemented,

	// election
	election_confirmed,
	election_expired,
	broadcast_vote,

	// blockprocessor
	block_processed,

	// vote_processor
	vote_processed,

	// network
	message_processed,
	message_sent,
	message_dropped,

	// election_scheduler
	block_activated,

	// vote_generator
	candidate_processed,
	should_vote,

	// bulk pull/push
	pulled_block,
	sending_block,
	sending_pending,
	sending_frontier,
	requesting_account_or_head,
	requesting_pending,

	// message types
	not_a_type,
	invalid,
	keepalive,
	publish,
	republish_vote,
	confirm_req,
	confirm_ack,
	node_id_handshake,
	telemetry_req,
	telemetry_ack,
	asc_pull_req,
	asc_pull_ack,
	bulk_pull,
	bulk_push,
	frontier_req,
	bulk_pull_account,

	_last // Must be the last enum
};

// TODO: Additionally categorize logs by categories which can be enabled/disabled independently
enum class category
{
	all = 0, // reserved

	work_generation,
	// ...

	_last // Must be the last enum
};

enum class tracing_format
{
	standard,
	json,
};
}

namespace nano::log
{
std::string_view to_string (nano::log::type);
std::string_view to_string (nano::log::detail);
std::string_view to_string (nano::log::level);

/// @throw std::invalid_argument if the input string does not match a log::level
nano::log::level parse_level (std::string_view);

/// @throw std::invalid_argument if the input string does not match a log::type
nano::log::type parse_type (std::string_view);

/// @throw std::invalid_argument if the input string does not match a log::detail
nano::log::detail parse_detail (std::string_view);

std::vector<nano::log::level> const & all_levels ();
std::vector<nano::log::type> const & all_types ();

std::string_view to_string (nano::log::tracing_format);
nano::log::tracing_format parse_tracing_format (std::string_view);
std::vector<nano::log::tracing_format> const & all_tracing_formats ();
}

// Ensure that the enum_range is large enough to hold all values (including future ones)
template <>
struct magic_enum::customize::enum_range<nano::log::type>
{
	static constexpr int min = 0;
	static constexpr int max = 128;
};

// Ensure that the enum_range is large enough to hold all values (including future ones)
template <>
struct magic_enum::customize::enum_range<nano::log::detail>
{
	static constexpr int min = 0;
	static constexpr int max = 512;
};