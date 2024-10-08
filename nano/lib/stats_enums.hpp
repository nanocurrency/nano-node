#pragma once

#include <cstdint>
#include <string_view>

#include <magic_enum.hpp>

namespace nano::stat
{
/** Primary statistics type */
enum class type
{
	_invalid = 0, // Default value, should not be used

	test,
	traffic_tcp,
	error,
	message,
	block,
	ledger,
	rollback,
	bootstrap,
	network,
	tcp_server,
	vote,
	vote_processor,
	vote_processor_tier,
	vote_processor_overfill,
	election,
	election_cleanup,
	election_vote,
	http_callback,
	ipc,
	tcp,
	tcp_channels,
	tcp_channels_rejected,
	tcp_channels_purge,
	tcp_listener,
	tcp_listener_rejected,
	channel,
	socket,
	confirmation_height,
	confirmation_observer,
	confirming_set,
	drop,
	aggregator,
	requests,
	request_aggregator,
	request_aggregator_vote,
	request_aggregator_replies,
	filter,
	telemetry,
	vote_generator,
	vote_cache,
	vote_cache_processor,
	hinting,
	blockprocessor,
	blockprocessor_source,
	blockprocessor_result,
	blockprocessor_overfill,
	bootstrap_ascending,
	bootstrap_ascending_accounts,
	bootstrap_ascending_verify,
	bootstrap_ascending_process,
	bootstrap_ascending_request,
	bootstrap_ascending_reply,
	bootstrap_ascending_next,
	bootstrap_server,
	bootstrap_server_request,
	bootstrap_server_overfill,
	bootstrap_server_response,
	active,
	active_elections,
	active_elections_started,
	active_elections_stopped,
	active_elections_confirmed,
	active_elections_dropped,
	active_elections_timeout,
	active_elections_cancelled,
	active_elections_cemented,
	backlog,
	unchecked,
	election_scheduler,
	election_bucket,
	optimistic_scheduler,
	handshake,
	rep_crawler,
	local_block_broadcaster,
	rep_tiers,
	syn_cookies,
	peer_history,
	port_mapping,
	message_processor,
	message_processor_overfill,
	message_processor_type,

	_last // Must be the last enum
};

/** Optional detail type */
enum class detail
{
	_invalid = 0, // Default value, should not be used

	// common
	all,
	ok,
	test,
	total,
	loop,
	loop_cleanup,
	process,
	processed,
	ignored,
	update,
	updated,
	inserted,
	erased,
	request,
	broadcast,
	cleanup,
	top,
	none,
	success,
	unknown,
	cache,
	rebroadcast,
	queue_overflow,
	triggered,
	notify,
	duplicate,
	confirmed,
	unconfirmed,
	cemented,
	cooldown,
	empty,

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
	rollback,
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

	// blockprocessor
	process_blocking,
	process_blocking_timeout,
	force,

	// block source
	live,
	live_originator,
	bootstrap,
	bootstrap_legacy,
	unchecked,
	local,
	forced,

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

	// dropped messages
	confirm_ack_zero_account,

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

	// vote result
	vote,
	valid,
	replay,
	indeterminate,

	// vote processor
	vote_overflow,
	vote_ignored,

	// election specific
	vote_new,
	vote_processed,
	vote_cached,
	election_block_conflict,
	election_restart,
	election_not_confirmed,
	election_hinted_overflow,
	election_hinted_confirmed,
	election_hinted_drop,
	broadcast_vote,
	broadcast_vote_normal,
	broadcast_vote_final,
	generate_vote,
	generate_vote_normal,
	generate_vote_final,
	broadcast_block_initial,
	broadcast_block_repeat,

	// election types
	manual,
	priority,
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
	message_size_too_big,
	outdated_version,

	// network
	loop_keepalive,
	loop_reachout,
	loop_reachout_cached,
	merge_peer,
	reachout_live,
	reachout_cached,

	// tcp
	tcp_write_drop,
	tcp_write_no_socket_drop,
	tcp_silent_connection_drop,
	tcp_io_timeout_drop,
	tcp_connect_error,
	tcp_read_error,
	tcp_write_error,

	// tcp_listener
	accept_success,
	accept_error,
	accept_failure,
	accept_rejected,
	close_error,
	max_per_ip,
	max_per_subnetwork,
	max_attempts,
	max_attempts_per_ip,
	excluded,
	erase_dead,
	connect_initiate,
	connect_failure,
	connect_error,
	connect_rejected,
	connect_success,
	attempt_timeout,
	not_a_peer,

	// tcp_channels
	channel_accepted,
	channel_rejected,
	channel_duplicate,
	idle,
	outdated,

	// tcp_server
	handshake,
	handshake_abort,
	handshake_error,
	handshake_network_error,
	handshake_initiate,
	handshake_response,
	handshake_response_invalid,

	// ipc
	invocations,

	// confirmation height
	blocks_confirmed,
	blocks_confirmed_unbounded,
	blocks_confirmed_bounded,

	// request aggregator
	aggregator_accepted,
	aggregator_dropped,

	// requests
	requests_cached_hashes,
	requests_generated_hashes,
	requests_cached_votes,
	requests_generated_votes,
	requests_cannot_vote,
	requests_unknown,
	requests_non_final,
	requests_final,

	// request_aggregator
	request_hashes,
	overfill_hashes,
	normal_vote,
	final_vote,

	// duplicate
	duplicate_publish_message,
	duplicate_confirm_ack_message,

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
	channel_full,
	frontiers,
	account_info,

	// backlog
	activated,
	activate_failed,
	activate_skip,
	activate_full,

	// active
	insert,
	insert_failed,
	election_cleanup,

	// active_elections
	started,
	stopped,

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
	account_info_empty,
	loop_database,
	loop_dependencies,
	duplicate_request,
	invalid_response_type,
	timestamp_reset,

	// bootstrap ascending accounts
	prioritize,
	prioritize_failed,
	block,
	unblock,
	unblock_failed,
	dependency_update,
	dependency_update_failed,

	next_none,
	next_priority,
	next_database,
	next_blocking,
	next_dependency,

	blocking_insert,
	blocking_erase_overflow,
	priority_insert,
	priority_erase_by_threshold,
	priority_erase_by_blocking,
	priority_erase_overflow,
	deprioritize,
	deprioritize_failed,
	sync_dependencies,

	request_blocks,
	request_account_info,

	// active
	started_hinted,
	started_optimistic,
	// rep_crawler
	channel_dead,
	query_target_failed,
	query_channel_busy,
	query_sent,
	query_duplicate,
	rep_timeout,
	query_timeout,
	query_completion,
	crawl_aggressive,
	crawl_normal,

	// block broadcaster
	broadcast_normal,
	broadcast_aggressive,
	erase_old,
	erase_confirmed,

	// rep tiers
	tier_1,
	tier_2,
	tier_3,

	// confirming_set
	notify_cemented,
	notify_already_cemented,
	notify_intermediate,
	already_cemented,
	cementing,
	cemented_hash,

	// election_state
	passive,
	active,
	expired_confirmed,
	expired_unconfirmed,
	cancelled,

	// election_status_type
	ongoing,
	active_confirmed_quorum,
	active_confirmation_height,
	inactive_confirmation_height,

	// election bucket
	activate_success,
	cancel_lowest,

	// query_type
	blocks_by_hash,
	blocks_by_account,
	account_info_by_hash,

	_last // Must be the last enum
};

/** Direction of the stat. If the direction is irrelevant, use in */
enum class dir
{
	in,
	out,

	_last // Must be the last enum
};

enum class sample
{
	_invalid = 0, // Default value, should not be used

	active_election_duration,
	bootstrap_tag_duration,
	rep_response_time,
	vote_generator_final_hashes,
	vote_generator_hashes,

	_last // Must be the last enum
};
}

namespace nano
{
std::string_view to_string (stat::type);
std::string_view to_string (stat::detail);
std::string_view to_string (stat::dir);
std::string_view to_string (stat::sample);
}

// Ensure that the enum_range is large enough to hold all values (including future ones)
template <>
struct magic_enum::customize::enum_range<nano::stat::type>
{
	static constexpr int min = 0;
	static constexpr int max = 128;
};

// Ensure that the enum_range is large enough to hold all values (including future ones)
template <>
struct magic_enum::customize::enum_range<nano::stat::detail>
{
	static constexpr int min = 0;
	static constexpr int max = 512;
};
