#include "rai/lib/errors.hpp"

std::string rai::error_common_messages::message (int ev) const
{
	switch (static_cast<rai::error_common> (ev))
	{
		case rai::error_common::generic:
			return "Unknown error";
		case rai::error_common::account_exists:
			return "Account already exists";
		case rai::error_common::account_not_found:
			return "Account not found";
		case rai::error_common::bad_account_number:
			return "Bad account number";
		case rai::error_common::bad_public_key:
			return "Bad public key";
		case rai::error_common::bad_seed:
			return "Bad seed";
		case rai::error_common::bad_wallet_number:
			return "Bad wallet number";
		case rai::error_common::bad_work_format:
			return "Bad work";
		case rai::error_common::invalid_work:
			return "Invalid work";
		case rai::error_common::invalid_index:
			return "Invalid index";
		case rai::error_common::numeric_conversion:
			return "Numeric conversion error";
		case rai::error_common::wallet_locked:
			return "Wallet is locked";
		case rai::error_common::wallet_not_found:
			return "Wallet not found";
	}
}

std::string rai::error_blocks_messages::message (int ev) const
{
	switch (static_cast<rai::error_blocks> (ev))
	{
		case rai::error_blocks::generic:
			return "Unknown error";
		case rai::error_blocks::bad_hash_number:
			return "Bad hash number";
		case rai::error_blocks::invalid_block_hash:
			return "Invalid block hash";
		case rai::error_blocks::not_found:
			return "Block not found";
	}
}
