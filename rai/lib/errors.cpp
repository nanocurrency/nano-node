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
		case nano::error_common::bad_account_number:
			return "Bad account number";
		case nano::error_common::bad_public_key:
			return "Bad public key";
		case nano::error_common::bad_seed:
			return "Bad seed";
		case nano::error_common::bad_wallet_number:
			return "Bad wallet number";
		case nano::error_common::bad_work_format:
			return "Bad work";
		case nano::error_common::invalid_work:
			return "Invalid work";
		case nano::error_common::invalid_index:
			return "Invalid index";
		case nano::error_common::numeric_conversion:
			return "Numeric conversion error";
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
		case nano::error_blocks::invalid_block_hash:
			return "Invalid block hash";
		case nano::error_blocks::not_found:
			return "Block not found";
	}

	return "Invalid error code";
}
