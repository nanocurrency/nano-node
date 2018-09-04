#include <rai/node/node.hpp>
#include <rai/rpc/rpc.hpp>
#include <rai/rpc/rpc_handler.hpp>

namespace
{
void reprocess_body (std::string & body, boost::property_tree::ptree & tree_a)
{
	std::stringstream stream;
	boost::property_tree::write_json (stream, tree_a);
	body = stream.str ();
}
}

void rai::rpc_handler::process_request ()
{
	try
	{
		auto max_depth_exceeded (false);
		auto max_depth_possible (0);
		for (auto ch : body)
		{
			if (ch == '[' || ch == '{')
			{
				if (max_depth_possible >= rpc.config.max_json_depth)
				{
					max_depth_exceeded = true;
					break;
				}
				++max_depth_possible;
			}
		}
		if (max_depth_exceeded)
		{
			error_response (response, "Max JSON depth exceeded");
		}
		else
		{
			std::stringstream istream (body);
			boost::property_tree::read_json (istream, request);
			std::string action (request.get<std::string> ("action"));
			if (action == "password_enter")
			{
				password_enter ();
				request.erase ("password");
				reprocess_body (body, request);
			}
			else if (action == "password_change")
			{
				password_change ();
				request.erase ("password");
				reprocess_body (body, request);
			}
			else if (action == "wallet_unlock")
			{
				password_enter ();
				request.erase ("password");
				reprocess_body (body, request);
			}
			if (node.config.logging.log_rpc ())
			{
				BOOST_LOG (node.log) << boost::str (boost::format ("%1% ") % request_id) << body;
			}
			if (action == "account_balance")
			{
				account_balance ();
			}
			else if (action == "account_block_count")
			{
				account_block_count ();
			}
			else if (action == "account_count")
			{
				account_count ();
			}
			else if (action == "account_create")
			{
				account_create ();
			}
			else if (action == "account_get")
			{
				account_get ();
			}
			else if (action == "account_history")
			{
				account_history ();
			}
			else if (action == "account_info")
			{
				account_info ();
			}
			else if (action == "account_key")
			{
				account_key ();
			}
			else if (action == "account_list")
			{
				account_list ();
			}
			else if (action == "account_move")
			{
				account_move ();
			}
			else if (action == "account_remove")
			{
				account_remove ();
			}
			else if (action == "account_representative")
			{
				account_representative ();
			}
			else if (action == "account_representative_set")
			{
				account_representative_set ();
			}
			else if (action == "account_weight")
			{
				account_weight ();
			}
			else if (action == "accounts_balances")
			{
				accounts_balances ();
			}
			else if (action == "accounts_create")
			{
				accounts_create ();
			}
			else if (action == "accounts_frontiers")
			{
				accounts_frontiers ();
			}
			else if (action == "accounts_pending")
			{
				accounts_pending ();
			}
			else if (action == "available_supply")
			{
				available_supply ();
			}
			else if (action == "block")
			{
				block ();
			}
			else if (action == "block_confirm")
			{
				block_confirm ();
			}
			else if (action == "blocks")
			{
				blocks ();
			}
			else if (action == "blocks_info")
			{
				blocks_info ();
			}
			else if (action == "block_account")
			{
				block_account ();
			}
			else if (action == "block_count")
			{
				block_count ();
			}
			else if (action == "block_count_type")
			{
				block_count_type ();
			}
			else if (action == "block_create")
			{
				block_create ();
			}
			else if (action == "block_hash")
			{
				block_hash ();
			}
			else if (action == "successors")
			{
				chain (true);
			}
			else if (action == "bootstrap")
			{
				bootstrap ();
			}
			else if (action == "bootstrap_any")
			{
				bootstrap_any ();
			}
			else if (action == "chain")
			{
				chain ();
			}
			else if (action == "delegators")
			{
				delegators ();
			}
			else if (action == "delegators_count")
			{
				delegators_count ();
			}
			else if (action == "deterministic_key")
			{
				deterministic_key ();
			}
			else if (action == "confirmation_history")
			{
				confirmation_history ();
			}
			else if (action == "frontiers")
			{
				frontiers ();
			}
			else if (action == "frontier_count")
			{
				account_count ();
			}
			else if (action == "history")
			{
				request.put ("head", request.get<std::string> ("hash"));
				account_history ();
			}
			else if (action == "keepalive")
			{
				keepalive ();
			}
			else if (action == "key_create")
			{
				key_create ();
			}
			else if (action == "key_expand")
			{
				key_expand ();
			}
			else if (action == "krai_from_raw")
			{
				mrai_from_raw (rai::kxrb_ratio);
			}
			else if (action == "krai_to_raw")
			{
				mrai_to_raw (rai::kxrb_ratio);
			}
			else if (action == "ledger")
			{
				ledger ();
			}
			else if (action == "mrai_from_raw")
			{
				mrai_from_raw ();
			}
			else if (action == "mrai_to_raw")
			{
				mrai_to_raw ();
			}
			else if (action == "password_change")
			{
				// Processed before logging
			}
			else if (action == "password_enter")
			{
				// Processed before logging
			}
			else if (action == "password_valid")
			{
				password_valid ();
			}
			else if (action == "payment_begin")
			{
				payment_begin ();
			}
			else if (action == "payment_init")
			{
				payment_init ();
			}
			else if (action == "payment_end")
			{
				payment_end ();
			}
			else if (action == "payment_wait")
			{
				payment_wait ();
			}
			else if (action == "peers")
			{
				peers ();
			}
			else if (action == "pending")
			{
				pending ();
			}
			else if (action == "pending_exists")
			{
				pending_exists ();
			}
			else if (action == "process")
			{
				process ();
			}
			else if (action == "rai_from_raw")
			{
				mrai_from_raw (rai::xrb_ratio);
			}
			else if (action == "rai_to_raw")
			{
				mrai_to_raw (rai::xrb_ratio);
			}
			else if (action == "receive")
			{
				receive ();
			}
			else if (action == "receive_minimum")
			{
				receive_minimum ();
			}
			else if (action == "receive_minimum_set")
			{
				receive_minimum_set ();
			}
			else if (action == "representatives")
			{
				representatives ();
			}
			else if (action == "representatives_online")
			{
				representatives_online ();
			}
			else if (action == "republish")
			{
				republish ();
			}
			else if (action == "search_pending")
			{
				search_pending ();
			}
			else if (action == "search_pending_all")
			{
				search_pending_all ();
			}
			else if (action == "send")
			{
				send ();
			}
			else if (action == "stats")
			{
				stats ();
			}
			else if (action == "stop")
			{
				stop ();
			}
			else if (action == "unchecked")
			{
				unchecked ();
			}
			else if (action == "unchecked_clear")
			{
				unchecked_clear ();
			}
			else if (action == "unchecked_get")
			{
				unchecked_get ();
			}
			else if (action == "unchecked_keys")
			{
				unchecked_keys ();
			}
			else if (action == "validate_account_number")
			{
				validate_account_number ();
			}
			else if (action == "version")
			{
				version ();
			}
			else if (action == "wallet_add")
			{
				wallet_add ();
			}
			else if (action == "wallet_add_watch")
			{
				wallet_add_watch ();
			}
			// Obsolete
			else if (action == "wallet_balance_total")
			{
				wallet_info ();
			}
			else if (action == "wallet_balances")
			{
				wallet_balances ();
			}
			else if (action == "wallet_change_seed")
			{
				wallet_change_seed ();
			}
			else if (action == "wallet_contains")
			{
				wallet_contains ();
			}
			else if (action == "wallet_create")
			{
				wallet_create ();
			}
			else if (action == "wallet_destroy")
			{
				wallet_destroy ();
			}
			else if (action == "wallet_export")
			{
				wallet_export ();
			}
			else if (action == "wallet_frontiers")
			{
				wallet_frontiers ();
			}
			else if (action == "wallet_info")
			{
				wallet_info ();
			}
			else if (action == "wallet_key_valid")
			{
				wallet_key_valid ();
			}
			else if (action == "wallet_ledger")
			{
				wallet_ledger ();
			}
			else if (action == "wallet_lock")
			{
				wallet_lock ();
			}
			else if (action == "wallet_locked")
			{
				password_valid (true);
			}
			else if (action == "wallet_pending")
			{
				wallet_pending ();
			}
			else if (action == "wallet_representative")
			{
				wallet_representative ();
			}
			else if (action == "wallet_representative_set")
			{
				wallet_representative_set ();
			}
			else if (action == "wallet_republish")
			{
				wallet_republish ();
			}
			else if (action == "wallet_unlock")
			{
				// Processed before logging
			}
			else if (action == "wallet_work_get")
			{
				wallet_work_get ();
			}
			else if (action == "work_generate")
			{
				work_generate ();
			}
			else if (action == "work_cancel")
			{
				work_cancel ();
			}
			else if (action == "work_get")
			{
				work_get ();
			}
			else if (action == "work_set")
			{
				work_set ();
			}
			else if (action == "work_validate")
			{
				work_validate ();
			}
			else if (action == "work_peer_add")
			{
				work_peer_add ();
			}
			else if (action == "work_peers")
			{
				work_peers ();
			}
			else if (action == "work_peers_clear")
			{
				work_peers_clear ();
			}
			else
			{
				error_response (response, "Unknown command");
			}
		}
	}
	catch (std::runtime_error const & err)
	{
		error_response (response, "Unable to parse JSON");
	}
	catch (...)
	{
		error_response (response, "Internal server error in RPC");
	}
}
