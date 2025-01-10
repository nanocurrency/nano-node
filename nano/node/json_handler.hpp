#pragma once

#include <celerix/lib/numbers.hpp>
#include <celerix/node/ipc/flatbuffers_handler.hpp>
#include <celerix/node/wallet.hpp>
#include <celerix/rpc/rpc.hpp>

#include <boost/property_tree/ptree.hpp>

#include <functional>
#include <string>

namespace celerix::secure
{
class transaction;
}

namespace celerix
{
namespace ipc
{
	class ipc_server;
}
class node;
class node_rpc_config;

class json_handler : public std::enable_shared_from_this<celerix::json_handler>
{
public:
	json_handler (
	celerix::node &, celerix::node_rpc_config const &, std::string const &, std::function<void (std::string const &)> const &, std::function<void ()> stop_callback = [] () {});
	void process_request (bool unsafe = false);
	void account_balance ();
	void account_block_count ();
	void account_count ();
	void account_create ();
	void account_get ();
	void account_history ();
	void account_info ();
	void account_key ();
	void account_list ();
	void account_move ();
	void account_remove ();
	void account_representative ();
	void account_representative_set ();
	void account_weight ();
	void accounts_balances ();
	void accounts_representatives ();
	void accounts_create ();
	void accounts_frontiers ();
	void accounts_pending ();
	void accounts_receivable ();
	void active_difficulty ();
	void election_statistics ();
	void available_supply ();
	void block_info ();
	void block_confirm ();
	void blocks ();
	void blocks_info ();
	void block_account ();
	void block_count ();
	void block_create ();
	void block_hash ();
	void bootstrap ();
	void bootstrap_any ();
	void bootstrap_lazy ();
	void bootstrap_status ();
	void chain (bool = false);
	void confirmation_active ();
	void confirmation_history ();
	void confirmation_info ();
	void confirmation_quorum ();
	void debug_bootstrap_priority_info ();
	void database_txn_tracker ();
	void delegators ();
	void delegators_count ();
	void deterministic_key ();
	void epoch_upgrade ();
	void frontiers ();
	void keepalive ();
	void key_create ();
	void key_expand ();
	void ledger ();
	void celerix_to_raw ();
	void raw_to_celerix ();
	void node_id ();
	void node_id_delete ();
	void password_change ();
	void password_enter ();
	void password_valid (bool = false);
	void peers ();
	void pending ();
	void pending_exists ();
	void receivable ();
	void receivable_exists ();
	void populate_backlog ();
	void process ();
	void pruned_exists ();
	void receive ();
	void receive_minimum ();
	void receive_minimum_set ();
	void representatives ();
	void representatives_online ();
	void republish ();
	void search_pending ();
	void search_receivable ();
	void search_pending_all ();
	void search_receivable_all ();
	void send ();
	void sign ();
	void stats ();
	void stats_clear ();
	void stop ();
	void telemetry ();
	void unchecked ();
	void unchecked_clear ();
	void unchecked_get ();
	void unchecked_keys ();
	void unopened ();
	void uptime ();
	void validate_account_number ();
	void version ();
	void wallet_add ();
	void wallet_add_watch ();
	void wallet_balances ();
	void wallet_change_seed ();
	void wallet_contains ();
	void wallet_create ();
	void wallet_destroy ();
	void wallet_export ();
	void wallet_frontiers ();
	void wallet_history ();
	void wallet_info ();
	void wallet_key_valid ();
	void wallet_ledger ();
	void wallet_lock ();
	void wallet_pending ();
	void wallet_receivable ();
	void wallet_representative ();
	void wallet_representative_set ();
	void wallet_republish ();
	void wallet_seed ();
	void wallet_work_get ();
	void work_cancel ();
	void work_generate ();
	void work_get ();
	void work_peer_add ();
	void work_peers ();
	void work_peers_clear ();
	void work_set ();
	void work_validate ();
	std::string body;
	celerix::node & node;
	boost::property_tree::ptree request;
	std::function<void (std::string const &)> response;
	void response_errors ();
	std::error_code ec;
	std::string action;
	boost::property_tree::ptree response_l;
	std::shared_ptr<celerix::wallet> wallet_impl ();
	bool wallet_locked_impl (store::transaction const &, std::shared_ptr<celerix::wallet> const &);
	bool wallet_account_impl (store::transaction const &, std::shared_ptr<celerix::wallet> const &, celerix::account const &);
	celerix::account account_impl (std::string = "", std::error_code = celerix::error_common::bad_account_number);
	celerix::account_info account_info_impl (secure::transaction const &, celerix::account const &);
	celerix::amount amount_impl ();
	std::shared_ptr<celerix::block> block_impl (bool = true);
	celerix::block_hash hash_impl (std::string = "hash");
	celerix::amount threshold_optional_impl ();
	uint64_t work_optional_impl ();
	uint64_t count_impl ();
	uint64_t count_optional_impl (uint64_t = std::numeric_limits<uint64_t>::max ());
	uint64_t offset_optional_impl (uint64_t = 0);
	uint64_t difficulty_optional_impl (celerix::work_version const);
	uint64_t difficulty_ledger (celerix::block const &);
	double multiplier_optional_impl (celerix::work_version const, uint64_t &);
	celerix::work_version work_version_optional_impl (celerix::work_version const default_a);
	bool enable_sign_hash{ false };
	std::function<void ()> stop_callback;
	celerix::node_rpc_config const & node_rpc_config;
	std::function<void ()> create_worker_task (std::function<void (std::shared_ptr<celerix::json_handler> const &)> const &);
};

class inprocess_rpc_handler final : public celerix::rpc_handler_interface
{
public:
	inprocess_rpc_handler (
	celerix::node & node_a, celerix::ipc::ipc_server & ipc_server_a, celerix::node_rpc_config const & node_rpc_config_a, std::function<void ()> stop_callback_a = [] () {}) :
		node (node_a),
		ipc_server (ipc_server_a),
		stop_callback (stop_callback_a),
		node_rpc_config (node_rpc_config_a)
	{
	}

	void process_request (std::string const &, std::string const & body_a, std::function<void (std::string const &)> response_a) override;
	void process_request_v2 (rpc_handler_request_params const & params_a, std::string const & body_a, std::function<void (std::shared_ptr<std::string> const &)> response_a) override;

	void stop () override
	{
		if (rpc)
		{
			rpc->stop ();
		}
	}

	void rpc_instance (celerix::rpc & rpc_a) override
	{
		rpc = rpc_a;
	}

private:
	celerix::node & node;
	celerix::ipc::ipc_server & ipc_server;
	boost::optional<celerix::rpc &> rpc;
	std::function<void ()> stop_callback;
	celerix::node_rpc_config const & node_rpc_config;
};
}
