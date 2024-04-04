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
	work,
	message_processing,
	vote_processing,
	block_processing,
	request_loop,
	wallet_actions,
	bootstrap_initiator,
	bootstrap_connections,
	voting,
	signature_checking,
	rpc_request_processor,
	rpc_process_container,
	confirmation_height_processing,
	worker,
	bootstrap_worker,
	request_aggregator,
	state_block_signature_verification,
	epoch_upgrader,
	db_parallel_traversal,
	unchecked,
	backlog_population,
	vote_generator_queue,
	bootstrap_server,
	telemetry,
	ascending_bootstrap,
	bootstrap_server_requests,
	bootstrap_server_responses,
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
}
