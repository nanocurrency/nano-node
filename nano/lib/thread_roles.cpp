#include <nano/lib/enum_util.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/lib/utility.hpp>

std::string_view nano::thread_role::to_string (nano::thread_role::name name)
{
	return nano::enum_util::name (name);
}

std::string nano::thread_role::get_string (nano::thread_role::name role)
{
	std::string thread_role_name_string;

	switch (role)
	{
		case nano::thread_role::name::unknown:
			thread_role_name_string = "<unknown>";
			break;
		case nano::thread_role::name::io:
			thread_role_name_string = "I/O";
			break;
		case nano::thread_role::name::work:
			thread_role_name_string = "Work pool";
			break;
		case nano::thread_role::name::message_processing:
			thread_role_name_string = "Msg processing";
			break;
		case nano::thread_role::name::vote_processing:
			thread_role_name_string = "Vote processing";
			break;
		case nano::thread_role::name::vote_cache_processing:
			thread_role_name_string = "Vote cache proc";
			break;
		case nano::thread_role::name::block_processing:
			thread_role_name_string = "Blck processing";
			break;
		case nano::thread_role::name::request_loop:
			thread_role_name_string = "Request loop";
			break;
		case nano::thread_role::name::wallet_actions:
			thread_role_name_string = "Wallet actions";
			break;
		case nano::thread_role::name::bootstrap_initiator:
			thread_role_name_string = "Bootstrap init";
			break;
		case nano::thread_role::name::bootstrap_connections:
			thread_role_name_string = "Bootstrap conn";
			break;
		case nano::thread_role::name::voting:
			thread_role_name_string = "Voting";
			break;
		case nano::thread_role::name::signature_checking:
			thread_role_name_string = "Signature check";
			break;
		case nano::thread_role::name::rpc_request_processor:
			thread_role_name_string = "RPC processor";
			break;
		case nano::thread_role::name::rpc_process_container:
			thread_role_name_string = "RPC process";
			break;
		case nano::thread_role::name::confirmation_height_processing:
			thread_role_name_string = "Conf height";
			break;
		case nano::thread_role::name::confirmation_height_notifications:
			thread_role_name_string = "Conf notif";
			break;
		case nano::thread_role::name::worker:
			thread_role_name_string = "Worker";
			break;
		case nano::thread_role::name::bootstrap_worker:
			thread_role_name_string = "Bootstrap work";
			break;
		case nano::thread_role::name::request_aggregator:
			thread_role_name_string = "Req aggregator";
			break;
		case nano::thread_role::name::state_block_signature_verification:
			thread_role_name_string = "State block sig";
			break;
		case nano::thread_role::name::epoch_upgrader:
			thread_role_name_string = "Epoch upgrader";
			break;
		case nano::thread_role::name::db_parallel_traversal:
			thread_role_name_string = "DB par traversl";
			break;
		case nano::thread_role::name::unchecked:
			thread_role_name_string = "Unchecked";
			break;
		case nano::thread_role::name::backlog_population:
			thread_role_name_string = "Backlog";
			break;
		case nano::thread_role::name::vote_generator_queue:
			thread_role_name_string = "Voting que";
			break;
		case nano::thread_role::name::ascending_bootstrap:
			thread_role_name_string = "Bootstrap asc";
			break;
		case nano::thread_role::name::bootstrap_server:
			thread_role_name_string = "Bootstrap serv";
			break;
		case nano::thread_role::name::telemetry:
			thread_role_name_string = "Telemetry";
			break;
		case nano::thread_role::name::scheduler_hinted:
			thread_role_name_string = "Sched Hinted";
			break;
		case nano::thread_role::name::scheduler_manual:
			thread_role_name_string = "Sched Manual";
			break;
		case nano::thread_role::name::scheduler_optimistic:
			thread_role_name_string = "Sched Opt";
			break;
		case nano::thread_role::name::scheduler_priority:
			thread_role_name_string = "Sched Priority";
			break;
		case nano::thread_role::name::stats:
			thread_role_name_string = "Stats";
			break;
		case nano::thread_role::name::rep_crawler:
			thread_role_name_string = "Rep Crawler";
			break;
		case nano::thread_role::name::local_block_broadcasting:
			thread_role_name_string = "Local broadcast";
			break;
		case nano::thread_role::name::rep_tiers:
			thread_role_name_string = "Rep tiers";
			break;
		case nano::thread_role::name::network_cleanup:
			thread_role_name_string = "Net cleanup";
			break;
		case nano::thread_role::name::network_keepalive:
			thread_role_name_string = "Net keepalive";
			break;
		case nano::thread_role::name::network_reachout:
			thread_role_name_string = "Net reachout";
			break;
		case nano::thread_role::name::signal_manager:
			thread_role_name_string = "Signal manager";
			break;
		case nano::thread_role::name::tcp_listener:
			thread_role_name_string = "TCP listener";
			break;
		case nano::thread_role::name::peer_history:
			thread_role_name_string = "Peer history";
			break;
		case nano::thread_role::name::port_mapping:
			thread_role_name_string = "Port mapping";
			break;
		case nano::thread_role::name::vote_router:
			thread_role_name_string = "Vote router";
			break;
		default:
			debug_assert (false && "nano::thread_role::get_string unhandled thread role");
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
thread_local nano::thread_role::name current_thread_role = nano::thread_role::name::unknown;
}

nano::thread_role::name nano::thread_role::get ()
{
	return current_thread_role;
}

std::string nano::thread_role::get_string ()
{
	return get_string (current_thread_role);
}

void nano::thread_role::set (nano::thread_role::name role)
{
	auto thread_role_name_string (get_string (role));

	nano::thread_role::set_os_name (thread_role_name_string);

	current_thread_role = role;
}
