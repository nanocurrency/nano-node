#pragma once

#include <string>

/*
 * Functions for understanding the role of the current thread
 */
namespace nano::thread_role
{
enum class name
{
	unknown,
	io,
	io_daemon,
	io_ipc,
	work,
	message_processing,
	vote_processing,
	vote_cache_processing,
	vote_rebroadcasting,
	block_processing,
	ledger_notifications,
	aec_loop,
	aec_notifications,
	wallet_actions,
	bootstrap_initiator,
	bootstrap_connections,
	voting,
	voting_final,
	signature_checking,
	rpc_request_processor,
	rpc_process_container,
	confirmation_height,
	confirmation_height_notifications,
	worker,
	wallet_worker,
	election_worker,
	request_aggregator,
	state_block_signature_verification,
	epoch_upgrader,
	db_parallel_traversal,
	unchecked,
	backlog_scan,
	bounded_backlog,
	bounded_backlog_scan,
	telemetry,
	bootstrap,
	bootstrap_database_scan,
	bootstrap_dependency_walker,
	bootstrap_frontier_scan,
	bootstrap_cleanup,
	bootstrap_worker,
	bootstrap_server,
	scheduler_hinted,
	scheduler_manual,
	scheduler_optimistic,
	scheduler_priority,
	rep_crawler,
	local_block_broadcasting,
	rep_tiers,
	network_cleanup,
	network_keepalive,
	network_reachout,
	signal_manager,
	tcp_listener,
	peer_history,
	port_mapping,
	stats,
	vote_router,
	online_reps,
	monitor,
	http_callbacks,
	pruning,
};

std::string_view to_string (name);

/*
 * Get/Set the identifier for the current thread
 */
nano::thread_role::name get ();
void set (nano::thread_role::name);

/*
 * Get the thread name as a string from enum
 */
std::string get_string (nano::thread_role::name);

/*
 * Get the current thread's role as a string
 */
std::string get_string ();

/*
 * Internal only, should not be called directly
 */
void set_os_name (std::string const &);

/*
 * Check if the current thread is a network IO thread
 */
bool is_network_io ();
}
