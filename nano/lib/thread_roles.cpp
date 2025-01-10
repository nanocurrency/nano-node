#include <celerix/lib/enum_util.hpp>
#include <celerix/lib/thread_roles.hpp>
#include <celerix/lib/utility.hpp>

std::string_view celerix::thread_role::to_string (celerix::thread_role::name name)
{
	return celerix::enum_util::name (name);
}

std::string celerix::thread_role::get_string (celerix::thread_role::name role)
{
	std::string thread_role_name_string;

	switch (role)
	{
		case celerix::thread_role::name::unknown:
			thread_role_name_string = "<unknown>";
			break;
		case celerix::thread_role::name::io:
			thread_role_name_string = "I/O";
			break;
		case celerix::thread_role::name::io_daemon:
			thread_role_name_string = "I/O (daemon)";
			break;
		case celerix::thread_role::name::io_ipc:
			thread_role_name_string = "I/O (IPC)";
			break;
		case celerix::thread_role::name::work:
			thread_role_name_string = "Work pool";
			break;
		case celerix::thread_role::name::message_processing:
			thread_role_name_string = "Msg processing";
			break;
		case celerix::thread_role::name::vote_processing:
			thread_role_name_string = "Vote processing";
			break;
		case celerix::thread_role::name::vote_cache_processing:
			thread_role_name_string = "Vote cache proc";
			break;
		case celerix::thread_role::name::block_processing:
			thread_role_name_string = "Blck processing";
			break;
		case celerix::thread_role::name::block_processing_notifications:
			thread_role_name_string = "Blck proc notif";
			break;
		case celerix::thread_role::name::request_loop:
			thread_role_name_string = "Request loop";
			break;
		case celerix::thread_role::name::wallet_actions:
			thread_role_name_string = "Wallet actions";
			break;
		case celerix::thread_role::name::bootstrap_initiator:
			thread_role_name_string = "Bootstrap init";
			break;
		case celerix::thread_role::name::bootstrap_connections:
			thread_role_name_string = "Bootstrap conn";
			break;
		case celerix::thread_role::name::voting:
			thread_role_name_string = "Voting";
			break;
		case celerix::thread_role::name::signature_checking:
			thread_role_name_string = "Signature check";
			break;
		case celerix::thread_role::name::rpc_request_processor:
			thread_role_name_string = "RPC processor";
			break;
		case celerix::thread_role::name::rpc_process_container:
			thread_role_name_string = "RPC process";
			break;
		case celerix::thread_role::name::confirmation_height:
			thread_role_name_string = "Conf height";
			break;
		case celerix::thread_role::name::confirmation_height_notifications:
			thread_role_name_string = "Conf notif";
			break;
		case celerix::thread_role::name::worker:
			thread_role_name_string = "Worker";
			break;
		case celerix::thread_role::name::wallet_worker:
			thread_role_name_string = "Wallet work";
			break;
		case celerix::thread_role::name::election_worker:
			thread_role_name_string = "Election work";
			break;
		case celerix::thread_role::name::request_aggregator:
			thread_role_name_string = "Req aggregator";
			break;
		case celerix::thread_role::name::state_block_signature_verification:
			thread_role_name_string = "State block sig";
			break;
		case celerix::thread_role::name::epoch_upgrader:
			thread_role_name_string = "Epoch upgrader";
			break;
		case celerix::thread_role::name::db_parallel_traversal:
			thread_role_name_string = "DB par traversl";
			break;
		case celerix::thread_role::name::unchecked:
			thread_role_name_string = "Unchecked";
			break;
		case celerix::thread_role::name::backlog_scan:
			thread_role_name_string = "Backlog scan";
			break;
		case celerix::thread_role::name::bounded_backlog:
			thread_role_name_string = "Bounded backlog";
			break;
		case celerix::thread_role::name::bounded_backlog_scan:
			thread_role_name_string = "Bounded b scan";
			break;
		case celerix::thread_role::name::bounded_backlog_notifications:
			thread_role_name_string = "Bounded b notif";
			break;
		case celerix::thread_role::name::vote_generator_queue:
			thread_role_name_string = "Voting que";
			break;
		case celerix::thread_role::name::bootstrap:
			thread_role_name_string = "Bootstrap";
			break;
		case celerix::thread_role::name::bootstrap_database_scan:
			thread_role_name_string = "Bootstrap db";
			break;
		case celerix::thread_role::name::bootstrap_dependency_walker:
			thread_role_name_string = "Bootstrap walkr";
			break;
		case celerix::thread_role::name::bootstrap_frontier_scan:
			thread_role_name_string = "Bootstrap front";
			break;
		case celerix::thread_role::name::bootstrap_cleanup:
			thread_role_name_string = "Bootstrap clean";
			break;
		case celerix::thread_role::name::bootstrap_worker:
			thread_role_name_string = "Bootstrap work";
			break;
		case celerix::thread_role::name::bootstrap_server:
			thread_role_name_string = "Bootstrap serv";
			break;
		case celerix::thread_role::name::telemetry:
			thread_role_name_string = "Telemetry";
			break;
		case celerix::thread_role::name::scheduler_hinted:
			thread_role_name_string = "Sched Hinted";
			break;
		case celerix::thread_role::name::scheduler_manual:
			thread_role_name_string = "Sched Manual";
			break;
		case celerix::thread_role::name::scheduler_optimistic:
			thread_role_name_string = "Sched Opt";
			break;
		case celerix::thread_role::name::scheduler_priority:
			thread_role_name_string = "Sched Priority";
			break;
		case celerix::thread_role::name::stats:
			thread_role_name_string = "Stats";
			break;
		case celerix::thread_role::name::rep_crawler:
			thread_role_name_string = "Rep Crawler";
			break;
		case celerix::thread_role::name::local_block_broadcasting:
			thread_role_name_string = "Local broadcast";
			break;
		case celerix::thread_role::name::rep_tiers:
			thread_role_name_string = "Rep tiers";
			break;
		case celerix::thread_role::name::network_cleanup:
			thread_role_name_string = "Net cleanup";
			break;
		case celerix::thread_role::name::network_keepalive:
			thread_role_name_string = "Net keepalive";
			break;
		case celerix::thread_role::name::network_reachout:
			thread_role_name_string = "Net reachout";
			break;
		case celerix::thread_role::name::signal_manager:
			thread_role_name_string = "Signal manager";
			break;
		case celerix::thread_role::name::tcp_listener:
			thread_role_name_string = "TCP listener";
			break;
		case celerix::thread_role::name::peer_history:
			thread_role_name_string = "Peer history";
			break;
		case celerix::thread_role::name::port_mapping:
			thread_role_name_string = "Port mapping";
			break;
		case celerix::thread_role::name::vote_router:
			thread_role_name_string = "Vote router";
			break;
		case celerix::thread_role::name::online_reps:
			thread_role_name_string = "Online reps";
			break;
		case celerix::thread_role::name::monitor:
			thread_role_name_string = "Monitor";
			break;
		default:
			debug_assert (false && "celerix::thread_role::get_string unhandled thread role");
	}

	/*
	 * We want to constrain the thread names to 15
	 * characters, since this is the smallest maximum
	 * length supported by the platforms we support
	 * (specifically, Linux)
	 */
	debug_assert (thread_role_name_string.size () < 16);
	return (thread_role_name_string);
}

namespace
{
thread_local celerix::thread_role::name current_thread_role = celerix::thread_role::name::unknown;
}

celerix::thread_role::name celerix::thread_role::get ()
{
	return current_thread_role;
}

std::string celerix::thread_role::get_string ()
{
	return get_string (current_thread_role);
}

void celerix::thread_role::set (celerix::thread_role::name role)
{
	auto thread_role_name_string = get_string (role);
	celerix::thread_role::set_os_name (thread_role_name_string); // Implementation is platform specific
	current_thread_role = role;
}

bool celerix::thread_role::is_network_io ()
{
	return celerix::thread_role::get () == celerix::thread_role::name::io;
}