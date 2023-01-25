#pragma once

#include <cstdint>
#include <string_view>

namespace nano::stat
{
/** Primary statistics type */
enum class type : uint8_t
{
	traffic_udp,
	traffic_tcp,
	error,
	message,
	block,
	ledger,
	rollback,
	bootstrap,
	tcp_server,
	vote,
	election,
	http_callback,
	peering,
	ipc,
	tcp,
	udp,
	confirmation_height,
	confirmation_observer,
	drop,
	aggregator,
	requests,
	filter,
	telemetry,
	vote_generator,
	vote_cache,
	hinting,
	blockprocessor,
	bootstrap_server,
	active,
	backlog,
	optimistic,

	_last // Must be the last enum
};

/** Optional detail type */
enum class detail : uint8_t
{
	all = 0,

	// common
	loop,
	total,
	process,
	update,
	insert,
	request,
	broadcast,

	// processing queue
	queue,
	overfill,
	batch,

	// error specific
	bad_sender,
	insufficient_work,
	http_callback,
	unreachable_host,
	invalid_network,

	// confirmation_observer specific
	active_quorum,
	active_conf_height,
	inactive_conf_height,

	// ledger, block, bootstrap
	send,
	receive,
	open,
	change,
	state_block,
	epoch_block,
	fork,
	old,
	gap_previous,
	gap_source,
	rollback_failed,
	progress,
	bad_signature,
	negative_spend,
	unreceivable,
	gap_epoch_open_pending,
	opened_burn_account,
	balance_mismatch,
	representative_mismatch,
	block_position,

	// message specific
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

	// bootstrap, callback
	initiate,
	initiate_legacy_age,
	initiate_lazy,
	initiate_wallet_lazy,

	// bootstrap specific
	bulk_pull,
	bulk_pull_account,
	bulk_pull_deserialize_receive_block,
	bulk_pull_error_starting_request,
	bulk_pull_failed_account,
	bulk_pull_receive_block_failure,
	bulk_pull_request_failure,
	bulk_push,
	frontier_req,
	frontier_confirmation_failed,
	frontier_confirmation_successful,
	error_socket_close,
	request_underflow,

	// vote specific
	vote_valid,
	vote_replay,
	vote_indeterminate,
	vote_invalid,
	vote_overflow,

	// election specific
	vote_new,
	vote_processed,
	vote_cached,
	late_block,
	late_block_seconds,
	election_start,
	election_confirmed_all,
	election_block_conflict,
	election_difficulty_update,
	election_drop_expired,
	election_drop_overflow,
	election_drop_all,
	election_restart,
	election_confirmed,
	election_not_confirmed,
	election_hinted_overflow,
	election_hinted_started,
	election_hinted_confirmed,
	election_hinted_drop,
	generate_vote,
	generate_vote_normal,
	generate_vote_final,

	// udp
	blocking,
	overflow,
	invalid_header,
	invalid_message_type,
	invalid_keepalive_message,
	invalid_publish_message,
	invalid_confirm_req_message,
	invalid_confirm_ack_message,
	invalid_node_id_handshake_message,
	invalid_telemetry_req_message,
	invalid_telemetry_ack_message,
	invalid_bulk_pull_message,
	invalid_bulk_pull_account_message,
	invalid_frontier_req_message,
	invalid_asc_pull_req_message,
	invalid_asc_pull_ack_message,
	message_too_big,
	outdated_version,
	udp_max_per_ip,
	udp_max_per_subnetwork,

	// tcp
	tcp_accept_success,
	tcp_accept_failure,
	tcp_write_drop,
	tcp_write_no_socket_drop,
	tcp_excluded,
	tcp_max_per_ip,
	tcp_max_per_subnetwork,
	tcp_silent_connection_drop,
	tcp_io_timeout_drop,
	tcp_connect_error,
	tcp_read_error,
	tcp_write_error,

	// ipc
	invocations,

	// peering
	handshake,

	// confirmation height
	blocks_confirmed,
	blocks_confirmed_unbounded,
	blocks_confirmed_bounded,

	// [request] aggregator
	aggregator_accepted,
	aggregator_dropped,

	// requests
	requests_cached_hashes,
	requests_generated_hashes,
	requests_cached_votes,
	requests_generated_votes,
	requests_cached_late_hashes,
	requests_cached_late_votes,
	requests_cannot_vote,
	requests_unknown,

	// duplicate
	duplicate_publish,

	// telemetry
	invalid_signature,
	different_genesis_hash,
	node_id_mismatch,
	genesis_mismatch,
	request_within_protection_cache_zone,
	no_response_received,
	unsolicited_telemetry_ack,
	failed_send_telemetry_req,
	empty_payload,
	cleanup_outdated,
	cleanup_dead,

	// vote generator
	generator_broadcasts,
	generator_replies,
	generator_replies_discarded,
	generator_spacing,

	// hinting
	hinted,
	insert,
	insert_failed,
	missing_block,

	// bootstrap server
	response,
	write_drop,
	write_error,
	blocks,
	drop,
	bad_count,
	response_blocks,
	response_account_info,
	channel_full,

	// backlog
	activated,
	
	// active
	started_hinted,
	started_optimistic,

	_last // Must be the last enum
};

/** Direction of the stat. If the direction is irrelevant, use in */
enum class dir : uint8_t
{
	in,
	out,

	_last // Must be the last enum
};
}

namespace nano
{
std::string_view to_string (stat::type type);
std::string_view to_string (stat::detail detail);
std::string_view to_string (stat::dir dir);
}