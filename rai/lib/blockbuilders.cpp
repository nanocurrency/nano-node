#include <rai/lib/blockbuilders.hpp>

namespace
{
template <typename BLOCKTYPE>
void previous_hex_impl (std::string const & previous_hex, std::error_code & ec, BLOCKTYPE & block)
{
	rai::block_hash previous;
	if (!previous.decode_hex (previous_hex))
	{
		block->hashables.previous = previous;
	}
	else
	{
		ec = nano::error_common::bad_previous;
	}
}

template <typename BLOCKTYPE>
void account_hex_impl (std::string const & account_hex, std::error_code & ec, BLOCKTYPE & block)
{
	rai::account account;
	if (!account.decode_hex (account_hex))
	{
		block->hashables.account = account;
	}
	else
	{
		ec = nano::error_common::bad_account_number;
	}
}

template <typename BLOCKTYPE>
void account_address_impl (std::string const & address, std::error_code & ec, BLOCKTYPE & block)
{
	rai::account account;
	if (!account.decode_account (address))
	{
		block->hashables.account = account;
	}
	else
	{
		ec = nano::error_common::bad_account_number;
	}
}

template <typename BLOCKTYPE>
void representative_hex_impl (std::string const & account_hex, std::error_code & ec, BLOCKTYPE & block)
{
	rai::account account;
	if (!account.decode_hex (account_hex))
	{
		block->hashables.representative = account;
	}
	else
	{
		ec = nano::error_common::bad_representative_number;
	}
}

template <typename BLOCKTYPE>
void representative_address_impl (std::string const & address, std::error_code & ec, BLOCKTYPE & block)
{
	rai::account account;
	if (!account.decode_account (address))
	{
		block->hashables.representative = account;
	}
	else
	{
		ec = nano::error_common::bad_representative_number;
	}
}

template <typename BLOCKTYPE>
void destination_hex_impl (std::string const & account_hex, std::error_code & ec, BLOCKTYPE & block)
{
	rai::account account;
	if (!account.decode_hex (account_hex))
	{
		block->hashables.destination = account;
	}
	else
	{
		ec = nano::error_common::bad_account_number;
	}
}

template <typename BLOCKTYPE>
void destination_address_impl (std::string const & address, std::error_code & ec, BLOCKTYPE & block)
{
	rai::account account;
	if (!account.decode_account (address))
	{
		block->hashables.destination = account;
	}
	else
	{
		ec = nano::error_common::bad_account_number;
	}
}

template <typename BLOCKTYPE>
void source_hex_impl (std::string const & source_hex, std::error_code & ec, BLOCKTYPE & block)
{
	rai::block_hash source;
	if (!source.decode_hex (source_hex))
	{
		block->hashables.source = source;
	}
	else
	{
		ec = nano::error_common::bad_source;
	}
}

template <typename BLOCKTYPE>
void balance_dec_impl (std::string const & balance_decimal, std::error_code & ec, BLOCKTYPE & block)
{
	rai::amount balance;
	if (!balance.decode_dec (balance_decimal))
	{
		block->hashables.balance = balance;
	}
	else
	{
		ec = nano::error_common::bad_balance;
	}
}

template <typename BLOCKTYPE>
void balance_hex_impl (std::string const & balance_hex, std::error_code & ec, BLOCKTYPE & block)
{
	rai::amount balance;
	if (!balance.decode_hex (balance_hex))
	{
		block->hashables.balance = balance;
	}
	else
	{
		ec = nano::error_common::bad_balance;
	}
}
}

rai::state_block_builder & rai::state_block_builder::clear ()
{
	block->work = uint64_t (0);
	block->signature.clear ();
	block->hashables.account.clear ();
	block->hashables.balance.clear ();
	block->hashables.link.clear ();
	block->hashables.previous.clear ();
	block->hashables.representative.clear ();
	return *this;
}

rai::state_block_builder & rai::state_block_builder::account (rai::account account)
{
	block->hashables.account = account;
	return *this;
}

rai::state_block_builder & rai::state_block_builder::account_hex (std::string account_hex)
{
	account_hex_impl (account_hex, ec, block);
	return *this;
}

rai::state_block_builder & rai::state_block_builder::account_address (std::string address)
{
	account_address_impl (address, ec, block);
	return *this;
}

rai::state_block_builder & rai::state_block_builder::representative (rai::account account)
{
	block->hashables.representative = account;
	return *this;
}

rai::state_block_builder & rai::state_block_builder::representative_hex (std::string account_hex)
{
	representative_hex_impl (account_hex, ec, block);
	return *this;
}

rai::state_block_builder & rai::state_block_builder::representative_address (std::string address)
{
	representative_address_impl (address, ec, block);
	return *this;
}

rai::state_block_builder & rai::state_block_builder::previous (rai::block_hash previous)
{
	block->hashables.previous = previous;
	return *this;
}

rai::state_block_builder & rai::state_block_builder::previous_hex (std::string previous_hex)
{
	previous_hex_impl (previous_hex, ec, block);
	return *this;
}

rai::state_block_builder & rai::state_block_builder::balance (rai::amount balance)
{
	block->hashables.balance = balance;
	return *this;
}

rai::state_block_builder & rai::state_block_builder::balance_dec (std::string balance_decimal)
{
	balance_dec_impl (balance_decimal, ec, block);
	return *this;
}

rai::state_block_builder & rai::state_block_builder::balance_hex (std::string balance_hex)
{
	balance_hex_impl (balance_hex, ec, block);
	return *this;
}

rai::state_block_builder & rai::state_block_builder::link (rai::uint256_union link)
{
	block->hashables.link = link;
	return *this;
}

rai::state_block_builder & rai::state_block_builder::link_hex (std::string link_hex)
{
	rai::uint256_union link;
	if (!link.decode_hex (link_hex))
	{
		block->hashables.link = link;
	}
	else
	{
		ec = nano::error_common::bad_link;
	}
	return *this;
}

rai::state_block_builder & rai::state_block_builder::link_address (std::string link_address)
{
	rai::account link;
	if (!link.decode_account (link_address))
	{
		block->hashables.link = link;
	}
	else
	{
		ec = nano::error_common::bad_link;
	}
	return *this;
}

rai::open_block_builder & rai::open_block_builder::clear ()
{
	block->work = uint64_t (0);
	block->signature.clear ();
	block->hashables.account.clear ();
	block->hashables.representative.clear ();
	block->hashables.source.clear ();
	return *this;
}

rai::open_block_builder & rai::open_block_builder::account (rai::account account)
{
	block->hashables.account = account;
	return *this;
}

rai::open_block_builder & rai::open_block_builder::account_hex (std::string account_hex)
{
	account_hex_impl (account_hex, ec, block);
	return *this;
}

rai::open_block_builder & rai::open_block_builder::account_address (std::string address)
{
	account_address_impl (address, ec, block);
	return *this;
}

rai::open_block_builder & rai::open_block_builder::representative (rai::account account)
{
	block->hashables.representative = account;
	return *this;
}

rai::open_block_builder & rai::open_block_builder::representative_hex (std::string account_hex)
{
	representative_hex_impl (account_hex, ec, block);
	return *this;
}

rai::open_block_builder & rai::open_block_builder::representative_address (std::string address)
{
	representative_address_impl (address, ec, block);
	return *this;
}

rai::open_block_builder & rai::open_block_builder::source (rai::block_hash source)
{
	block->hashables.source = source;
	return *this;
}

rai::open_block_builder & rai::open_block_builder::source_hex (std::string source_hex)
{
	source_hex_impl (source_hex, ec, block);
	return *this;
}

rai::change_block_builder & rai::change_block_builder::clear ()
{
	block->work = uint64_t (0);
	block->signature.clear ();
	block->hashables.previous.clear ();
	block->hashables.representative.clear ();
	return *this;
}

rai::change_block_builder & rai::change_block_builder::representative (rai::account account)
{
	block->hashables.representative = account;
	return *this;
}

rai::change_block_builder & rai::change_block_builder::representative_hex (std::string account_hex)
{
	representative_hex_impl (account_hex, ec, block);
	return *this;
}

rai::change_block_builder & rai::change_block_builder::representative_address (std::string address)
{
	representative_address_impl (address, ec, block);
	return *this;
}

rai::change_block_builder & rai::change_block_builder::previous (rai::block_hash previous)
{
	block->hashables.previous = previous;
	return *this;
}

rai::change_block_builder & rai::change_block_builder::previous_hex (std::string previous_hex)
{
	previous_hex_impl (previous_hex, ec, block);
	return *this;
}

rai::send_block_builder & rai::send_block_builder::clear ()
{
	block->work = uint64_t (0);
	block->signature.clear ();
	block->hashables.previous.clear ();
	block->hashables.destination.clear ();
	block->hashables.balance.clear ();
	return *this;
}

rai::send_block_builder & rai::send_block_builder::destination (rai::account account)
{
	block->hashables.destination = account;
	return *this;
}

rai::send_block_builder & rai::send_block_builder::destination_hex (std::string account_hex)
{
	destination_hex_impl (account_hex, ec, block);
	return *this;
}

rai::send_block_builder & rai::send_block_builder::destination_address (std::string address)
{
	destination_address_impl (address, ec, block);
	return *this;
}

rai::send_block_builder & rai::send_block_builder::previous (rai::block_hash previous)
{
	block->hashables.previous = previous;
	return *this;
}

rai::send_block_builder & rai::send_block_builder::previous_hex (std::string previous_hex)
{
	previous_hex_impl (previous_hex, ec, block);
	return *this;
}

rai::send_block_builder & rai::send_block_builder::balance (rai::amount balance)
{
	block->hashables.balance = balance;
	return *this;
}

rai::send_block_builder & rai::send_block_builder::balance_dec (std::string balance_decimal)
{
	balance_dec_impl (balance_decimal, ec, block);
	return *this;
}

rai::send_block_builder & rai::send_block_builder::balance_hex (std::string balance_hex)
{
	balance_hex_impl (balance_hex, ec, block);
	return *this;
}

rai::receive_block_builder & rai::receive_block_builder::clear ()
{
	block->work = uint64_t (0);
	block->signature.clear ();
	block->hashables.previous.clear ();
	block->hashables.source.clear ();
	return *this;
}

rai::receive_block_builder & rai::receive_block_builder::previous (rai::block_hash previous)
{
	block->hashables.previous = previous;
	return *this;
}

rai::receive_block_builder & rai::receive_block_builder::previous_hex (std::string previous_hex)
{
	previous_hex_impl (previous_hex, ec, block);
	return *this;
}

rai::receive_block_builder & rai::receive_block_builder::source (rai::block_hash source)
{
	block->hashables.source = source;
	return *this;
}

rai::receive_block_builder & rai::receive_block_builder::source_hex (std::string source_hex)
{
	source_hex_impl (source_hex, ec, block);
	return *this;
}
