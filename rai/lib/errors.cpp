#include "rai/lib/errors.hpp"

std::string nano::error_common_messages::message (int ev) const
{
	switch (static_cast<nano::error_common> (ev))
	{
		case nano::error_common::generic:
			return "Unknown error";
		case nano::error_common::account_exists:
			return "Account already exists";
		case nano::error_common::account_not_found:
			return "Account not found";
		case nano::error_common::account_not_found_wallet:
			return "Account not found in wallet";
		case nano::error_common::bad_account_number:
			return "Bad account number";
		case nano::error_common::bad_private_key:
			return "Bad private key";
		case nano::error_common::bad_public_key:
			return "Bad public key";
		case nano::error_common::bad_seed:
			return "Bad seed";
		case nano::error_common::bad_threshold:
			return "Bad threshold number";
		case nano::error_common::bad_wallet_number:
			return "Bad wallet number";
		case nano::error_common::bad_work_format:
			return "Bad work";
		case nano::error_common::insufficient_balance:
			return "Insufficient balance";
		case nano::error_common::invalid_amount:
			return "Invalid amount number";
		case nano::error_common::invalid_amount_big:
			return "Amount too big";
		case nano::error_common::invalid_count:
			return "Invalid count";
		case nano::error_common::invalid_ip_address:
			return "Invalid IP address";
		case nano::error_common::invalid_port:
			return "Invalid port";
		case nano::error_common::invalid_index:
			return "Invalid index";
		case nano::error_common::invalid_work:
			return "Invalid work";
		case nano::error_common::numeric_conversion:
			return "Numeric conversion error";
		case nano::error_common::wallet_lmdb_max_dbs:
			return "Failed to create wallet. Increase lmdb_max_dbs in node config";
		case nano::error_common::wallet_locked:
			return "Wallet is locked";
		case nano::error_common::wallet_not_found:
			return "Wallet not found";
	}

	return "Invalid error code";
}

std::string nano::error_blocks_messages::message (int ev) const
{
	switch (static_cast<nano::error_blocks> (ev))
	{
		case nano::error_blocks::generic:
			return "Unknown error";
		case nano::error_blocks::bad_hash_number:
			return "Bad hash number";
		case nano::error_blocks::invalid_block:
			return "Block is invalid";
		case nano::error_blocks::invalid_block_hash:
			return "Invalid block hash";
		case nano::error_blocks::invalid_type:
			return "Invalid block type";
		case nano::error_blocks::not_found:
			return "Block not found";
		case nano::error_blocks::work_low:
			return "Block work is less than threshold";
	}

	return "Invalid error code";
}

std::string nano::error_rpc_messages::message (int ev) const
{
	switch (static_cast<nano::error_rpc> (ev))
	{
		case nano::error_rpc::generic:
			return "Unknown error";
		case nano::error_rpc::bad_destination:
			return "Bad destination account";
		case nano::error_rpc::bad_key:
			return "Bad key";
		case nano::error_rpc::bad_link:
			return "Bad link number";
		case nano::error_rpc::bad_previous:
			return "Bad previous";
		case nano::error_rpc::bad_representative_number:
			return "Bad representative number";
		case nano::error_rpc::bad_source:
			return "Bad source";
		case nano::error_rpc::bad_timeout:
			return "Bad timeout number";
		case nano::error_rpc::block_create_balance_mismatch:
			return "Balance mismatch for previous block";
		case nano::error_rpc::block_create_key_required:
			return "Private key or local wallet and account required";
		case nano::error_rpc::block_create_public_key_mismatch:
			return "Incorrect key for given account";
		case nano::error_rpc::block_create_requirements_state:
			return "Previous, representative, final balance and link (source or destination) are required";
		case nano::error_rpc::block_create_requirements_open:
			return "Representative account and source hash required";
		case nano::error_rpc::block_create_requirements_receive:
			return "Previous hash and source hash required";
		case nano::error_rpc::block_create_requirements_change:
			return "Representative account and previous hash required";
		case nano::error_rpc::block_create_requirements_send:
			return "Destination account, previous hash, current balance and amount required";
		case nano::error_rpc::invalid_balance:
			return "Invalid balance number";
		case nano::error_rpc::invalid_destinations:
			return "Invalid destinations number";
		case nano::error_rpc::invalid_offset:
			return "Invalid offset";
		case nano::error_rpc::invalid_missing_type:
			return "Invalid or missing type argument";
		case nano::error_rpc::invalid_sources:
			return "Invalid sources number";
		case nano::error_rpc::payment_account_balance:
			return "Account has non-zero balance";
		case nano::error_rpc::payment_unable_create_account:
			return "Unable to create transaction account";
		case nano::error_rpc::rpc_control_disabled:
			return "RPC control is disabled";
		case nano::error_rpc::source_not_found:
			return "Source not found";
	}

	return "Invalid error code";
}

std::string nano::error_process_messages::message (int ev) const
{
	switch (static_cast<nano::error_process> (ev))
	{
		case nano::error_process::generic:
			return "Unknown error";
		case nano::error_process::bad_signature:
			return "Bad signature";
		case nano::error_process::old:
			return "Old block";
		case nano::error_process::negative_spend:
			return "Negative spend";
		case nano::error_process::fork:
			return "Fork";
		case nano::error_process::unreceivable:
			return "Unreceivable";
		case nano::error_process::gap_previous:
			return "Gap previous block";
		case nano::error_process::gap_source:
			return "Gap source block";
		case nano::error_process::opened_burn_account:
			return "Burning account";
		case nano::error_process::balance_mismatch:
			return "Balance and amount delta do not match";
		case nano::error_process::block_position:
			return "This block cannot follow the previous block";
		case nano::error_process::other:
			"Error processing block";
	}

	return "Invalid error code";
}
