#include <boost/endian/conversion.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <nano/lib/errors.hpp>
#include <nano/lib/ipc_client.hpp>
#include <nano/lib/rpcconfig.hpp>
#include <nano/rpc/rpc_handler.hpp>
#include <nano/rpc/rpc_request_processor.hpp>
#include <unordered_set>

namespace
{
std::unordered_set<std::string> create_rpc_control_impls ();
std::unordered_set<std::string> rpc_control_impl_set = create_rpc_control_impls ();
}

nano::rpc_handler::rpc_handler (nano::rpc_config const & rpc_config, std::string const & body_a, std::string const & request_id_a, std::function<void(std::string const &)> const & response_a, nano::rpc_request_processor & rpc_request_processor_a) :
body (body_a),
request_id (request_id_a),
response (response_a),
rpc_config (rpc_config),
rpc_request_processor (rpc_request_processor_a)
{
}

void nano::rpc_handler::process_request ()
{
	try
	{
		auto max_depth_exceeded (false);
		auto max_depth_possible (0u);
		for (auto ch : body)
		{
			if (ch == '[' || ch == '{')
			{
				if (max_depth_possible >= rpc_config.max_json_depth)
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
			// Check if this is a RPC command which requires RPC enabled control
			std::stringstream ss;
			ss << body;
			boost::property_tree::ptree tree;
			boost::property_tree::read_json (ss, tree);

			auto action = tree.get<std::string> ("action");

			std::error_code rpc_control_disabled_ec = nano::error_rpc::rpc_control_disabled;

			bool error = false;
			auto found = rpc_control_impl_set.find (action);
			if (found != rpc_control_impl_set.cend () && !rpc_config.enable_control)
			{
				error_response (response, rpc_control_disabled_ec.message ());
				error = true;
			}
			else
			{
				// Special case with stats, type -> objects
				if (action == "stats" && !rpc_config.enable_control)
				{
					if (tree.get<std::string> ("type") == "objects")
					{
						error_response (response, rpc_control_disabled_ec.message ());
						error = true;
					}
				}
				else if (action == "process")
				{
					auto force = tree.get_optional<bool> ("force");
					if (force && !rpc_config.enable_control)
					{
						error_response (response, rpc_control_disabled_ec.message ());
						error = true;
					}
				}
			}

			if (!error)
			{
				// Add request (currently assumes json) to the rpc processor queue
				rpc_request_processor.add (std::make_shared<nano::rpc_request> (action, body, this->response));
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error_response (response, "Unable to parse JSON");
	}
	catch (...)
	{
		error_response (response, "Internal server error in RPC");
	}
}

namespace nano
{
void error_response (std::function<void(std::string const &)> response_a, std::string const & message_a)
{
	boost::property_tree::ptree response_l;
	response_l.put ("error", message_a);

	std::stringstream ss;
	boost::property_tree::write_json (ss, response_l);
	response_a (ss.str ());
}
}

namespace
{
std::unordered_set<std::string> create_rpc_control_impls ()
{
	std::unordered_set<std::string> set;
	set.emplace ("account_create");
	set.emplace ("account_move");
	set.emplace ("account_remove");
	set.emplace ("account_representative_set");
	set.emplace ("accounts_create");
	set.emplace ("block_create");
	set.emplace ("bootstrap_lazy");
	set.emplace ("keepalive");
	set.emplace ("ledger");
	set.emplace ("node_id");
	set.emplace ("node_id_delete");
	set.emplace ("password_change");
	set.emplace ("receive");
	set.emplace ("receive_minimum");
	set.emplace ("receive_minimum_set");
	set.emplace ("search_pending");
	set.emplace ("search_pending_all");
	set.emplace ("send");
	set.emplace ("stop");
	set.emplace ("unchecked_clear");
	set.emplace ("unopened");
	set.emplace ("wallet_add");
	set.emplace ("wallet_add_watch");
	set.emplace ("wallet_change_seed");
	set.emplace ("wallet_create");
	set.emplace ("wallet_destroy");
	set.emplace ("wallet_lock");
	set.emplace ("wallet_representative_set");
	set.emplace ("wallet_republish");
	set.emplace ("wallet_work_get");
	set.emplace ("work_generate");
	set.emplace ("work_cancel");
	set.emplace ("work_get");
	set.emplace ("work_set");
	set.emplace ("work_peer_add");
	set.emplace ("work_peers");
	set.emplace ("work_peers_clear");
	return set;
}
}
