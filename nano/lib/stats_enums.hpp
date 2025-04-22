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
	error,
	message,
	message_loopback,
	block,
	ledger,
	ledger_notifications,
	rollback,
	network,
	vote,
	vote_processor,
	vote_processor_tier,
	vote_processor_overfill,
	vote_rebroadcaster,
	vote_rebroadcaster_tier,
	election,
	election_cleanup,
	election_vote,
	http_callbacks,
	http_callbacks_notified,
	http_callbacks_ec,
	ipc,
	tcp,
	tcp_server,
	tcp_channel,
	tcp_channel_queued,
	tcp_channel_send,
	tcp_channel_drop,
	tcp_channel_ec,
	tcp_channel_wait,
	tcp_channels,
	tcp_channels_rejected,
	tcp_channels_purge,
	tcp_listener,
	tcp_listener_rejected,
	traffic_tcp,
	traffic_tcp_type,
	channel,
	socket,
	confirmation_height,
	confirmation_observer,
	cementing_set,
	drop, // TODO: Rename to message_drop
	aggregator,
	requests,
	request_aggregator,
	request_aggregator_vote,
	request_aggregator_replies,
	filter,
	telemetry,
	vote_generator,
	vote_generator_final,
	vote_cache,
	vote_cache_processor,
	hinting,
	block_processor,
	block_processor_source,
	block_processor_result,
	block_processor_overfill,
	bootstrap,
	bootstrap_verify,
	bootstrap_verify_blocks,
	bootstrap_verify_frontiers,
	bootstrap_process,
	bootstrap_request,
	bootstrap_request_ec,
	bootstrap_request_blocks,
	bootstrap_reply,
	bootstrap_next,
	bootstrap_frontiers,
	bootstrap_account_sets,
	bootstrap_frontier_scan,
	bootstrap_timeout,
	bootstrap_server,
	bootstrap_server_request,
	bootstrap_server_overfill,
	bootstrap_server_response,
	bootstrap_server_send,
	bootstrap_server_ec,
	active,
	active_elections,
	active_elections_started,
	active_elections_stopped,
	active_elections_confirmed,
	active_elections_dropped,
	active_elections_timeout,
	active_elections_cancelled,
	active_elections_cemented,
	backlog_scan,
	bounded_backlog,
	backlog,
	unchecked,
	election_scheduler,
	election_bucket,
	optimistic_scheduler,
	handshake,
	rep_crawler,
	rep_crawler_ec,
	local_block_broadcaster,
	rep_tiers,
	syn_cookies,
	peer_history,
	port_mapping,
	message_processor,
	message_processor_overfill,
	message_processor_type,
	process_confirmed,
	online_reps,
	pruning,
	fork_cache,

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
	request_failed,
	request_success,
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
	done,
	retry,
	prioritized,
	pending,
	sync,
	requeued,
	evicted,
	other,
	drop,
	queued,
	error,
	failed,
	refresh,
	sent,
	reset,

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

	// block_processor
	process_blocking,
	process_blocking_timeout,
	force,
	cooldown_backlog,

	// block source
	live,
	live_originator,
	bootstrap,
	bootstrap_legacy,
	unchecked,
	local,
	forced,
	election,

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
	late,

	// vote processor
	vote_overflow,
	vote_ignored,

	// vote_rebroadcaster
	cleanup_tiers,
	representatives_full,
	representatives_erase_lowest,
	representatives_erase_stale,
	already_rebroadcasted,
	rebroadcast_unnecessary,
	rebroadcast_hashes,

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
	confirm_once,
	confirm_once_failed,
	confirmation_request,

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
	merge_peer_failed,
	reachout_live,
	reachout_cached,
	connected,

	// traffic type
	generic,
	bootstrap_server,
	bootstrap_requests,
	block_broadcast,
	block_broadcast_initial,
	block_broadcast_rpc,
	confirmation_requests,
	vote_rebroadcast,
	vote_reply,
	rep_crawler,
	telemetry,

	// tcp
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

	// tcp_channel
	wait_socket,
	wait_bandwidth,

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
	erase_stale,

	// vote_generator
	generator_broadcasts,
	generator_replies,
	generator_replies_discarded,
	generator_spacing,
	sent_pr,
	sent_non_pr,

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
	scanned,

	// active_elections
	insert,
	insert_failed,
	transition_priority,
	transition_priority_failed,
	election_cleanup,
	activate_immediately,
	started,
	stopped,
	confirm_dependent,
	forks_cached,
	bootstrap_stale,

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

	// bootstrap
	missing_tag,
	reply,
	throttled,
	track,
	timeout,
	nothing_new,
	account_info_empty,
	frontiers_empty,
	loop_database,
	loop_dependencies,
	loop_frontiers,
	loop_frontiers_processing,
	duplicate_request,
	invalid_response_type,
	invalid_response,
	timestamp_reset,
	processing_frontiers,
	frontiers_dropped,
	sync_accounts,

	prioritize,
	prioritize_failed,
	block,
	block_failed,
	unblock,
	unblock_failed,
	dependency_update,
	dependency_update_failed,

	done_range,
	done_empty,
	next_by_requests,
	next_by_timestamp,
	advance,
	advance_failed,

	next_none,
	next_priority,
	next_database,
	next_blocking,
	next_dependency,
	next_frontier,

	blocking_insert,
	blocking_overflow,
	priority_insert,
	priority_set,
	priority_erase,
	priority_unblocked,
	erase_by_threshold,
	erase_by_blocking,
	priority_overflow,
	deprioritize,
	deprioritize_failed,
	sync_dependencies,
	decay_blocking,
	blocking_decayed,
	dependency_synced,

	request_blocks,
	request_account_info,

	safe,
	base,

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

	// ledger_notifications
	notify_processed,
	notify_rolled_back,

	// cementing_set
	notify_cemented,
	notify_already_cemented,
	notify_intermediate,
	already_cemented,
	cementing,
	cemented_hash,
	cementing_failed,
	deferred_failed,

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

	// bounded backlog,
	gathered_targets,
	performing_rollbacks,
	no_targets,
	rollback_missing_block,
	rollback_skipped,
	loop_scan,

	// online_reps
	trim_trend,
	sanitize_old,
	sanitize_future,
	sample,
	rep_new,
	rep_update,
	rep_trim,
	update_online,

	// error codes
	no_buffer_space,
	timed_out,
	host_unreachable,
	not_supported,

	// http
	error_resolving,
	error_connecting,
	error_sending,
	error_completing,
	bad_status,

	// http_callbacks
	block_confirmed,
	large_backlog,

	// pruning
	ledger_pruning,
	pruning_target,
	pruned_count,
	collect_targets,

	// fork_cache
	overfill_entry,

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
