#pragma once

#include <cstdint>
#include <string_view>

namespace nano::stat
{
/** Primary statistics type */
enum class type : uint8_t
{
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
	ipc,
	tcp,
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
	active_started,
	active_confirmed,
	active_dropped,
	active_timeout,
	backlog,
	unchecked,
	election_scheduler,
	optimistic_scheduler,
	handshake,

	bootstrap_ascending,
	bootstrap_ascending_accounts,

	_last // Must be the last enum
};

/** Optional detail type */
enum class detail : uint8_t
{
	all = 0,

	// common
	ok,
	loop,
	total,
	process,
	update,
	request,
	broadcast,
	cleanup,
	top,

	// processing queue
	queue,
	overfill,
	batch,

	// error specific
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
	bulk_pull_error_starting_request,
	bulk_pull_failed_account,
	bulk_pull_request_failure,
	bulk_push,
	frontier_req,
	frontier_confirmation_failed,
	error_socket_close,

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
	election_block_conflict,
	generate_vote,
	generate_vote_normal,
	generate_vote_final,

	// election types
	normal,
	hinted,
	optimistic,

	// received messages
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
	requests_cannot_vote,
	requests_unknown,

	// duplicate
	duplicate_publish,

	// telemetry
	invalid_signature,
	node_id_mismatch,
	genesis_mismatch,
	request_within_protection_cache_zone,
	no_response_received,
	unsolicited_telemetry_ack,
	failed_send_telemetry_req,
	empty_payload,
	cleanup_outdated,

	// vote generator
	generator_broadcasts,
	generator_replies,
	generator_replies_discarded,
	generator_spacing,

	// hinting
	missing_block,
	dependent_unconfirmed,
	already_confirmed,
	activate,
	activate_immediate,
	dependent_activated,

	// bootstrap server
	response,
	write_error,
	blocks,
	response_blocks,
	response_account_info,
	channel_full,

	// backlog
	activated,

	// active
	insert,
	insert_failed,

	// unchecked
	put,
	satisfied,
	trigger,

	// election scheduler
	insert_manual,
	insert_priority,
	insert_priority_success,
	erase_oldest,

	// handshake
	invalid_node_id,
	missing_cookie,
	invalid_genesis,

	// bootstrap ascending
	missing_tag,
	reply,
	throttled,
	track,
	timeout,
	nothing_new,

	// bootstrap ascending accounts
	prioritize,
	prioritize_failed,
	block,
	unblock,
	unblock_failed,

	next_priority,
	next_database,
	next_none,

	blocking_insert,
	blocking_erase_overflow,
	priority_insert,
	priority_erase_threshold,
	priority_erase_block,
	priority_erase_overflow,
	deprioritize,
	deprioritize_failed,

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
