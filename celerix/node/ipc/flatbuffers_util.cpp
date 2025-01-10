#include <celerix/lib/block_type.hpp>
#include <celerix/lib/blocks.hpp>
#include <celerix/lib/numbers.hpp>
#include <celerix/node/ipc/flatbuffers_util.hpp>
#include <celerix/secure/common.hpp>

std::unique_ptr<celerixapi::BlockStateT> celerix::ipc::flatbuffers_builder::from (celerix::state_block const & block_a, celerix::amount const & amount_a, bool is_state_send_a, bool is_state_epoch_a)
{
	auto block (std::make_unique<celerixapi::BlockStateT> ());
	block->account = block_a.account ().to_account ();
	block->hash = block_a.hash ().to_string ();
	block->previous = block_a.previous ().to_string ();
	block->representative = block_a.representative_field ().value ().to_account ();
	block->balance = block_a.balance ().to_string_dec ();
	block->link = block_a.link_field ().value ().to_string ();
	block->link_as_account = block_a.link_field ().value ().to_account ();
	block_a.signature.encode_hex (block->signature);
	block->work = celerix::to_string_hex (block_a.work);

	if (is_state_send_a)
	{
		block->subtype = celerixapi::BlockSubType::BlockSubType_send;
	}
	else if (block_a.is_change ())
	{
		block->subtype = celerixapi::BlockSubType::BlockSubType_change;
	}
	else if (amount_a == 0 && is_state_epoch_a)
	{
		block->subtype = celerixapi::BlockSubType::BlockSubType_epoch;
	}
	else
	{
		block->subtype = celerixapi::BlockSubType::BlockSubType_receive;
	}
	return block;
}

std::unique_ptr<celerixapi::BlockSendT> celerix::ipc::flatbuffers_builder::from (celerix::send_block const & block_a)
{
	auto block (std::make_unique<celerixapi::BlockSendT> ());
	block->hash = block_a.hash ().to_string ();
	block->balance = block_a.balance ().to_string_dec ();
	block->destination = block_a.hashables.destination.to_account ();
	block->previous = block_a.previous ().to_string ();
	block_a.signature.encode_hex (block->signature);
	block->work = celerix::to_string_hex (block_a.work);
	return block;
}

std::unique_ptr<celerixapi::BlockReceiveT> celerix::ipc::flatbuffers_builder::from (celerix::receive_block const & block_a)
{
	auto block (std::make_unique<celerixapi::BlockReceiveT> ());
	block->hash = block_a.hash ().to_string ();
	block->source = block_a.source_field ().value ().to_string ();
	block->previous = block_a.previous ().to_string ();
	block_a.signature.encode_hex (block->signature);
	block->work = celerix::to_string_hex (block_a.work);
	return block;
}

std::unique_ptr<celerixapi::BlockOpenT> celerix::ipc::flatbuffers_builder::from (celerix::open_block const & block_a)
{
	auto block (std::make_unique<celerixapi::BlockOpenT> ());
	block->hash = block_a.hash ().to_string ();
	block->source = block_a.source_field ().value ().to_string ();
	block->account = block_a.account ().to_account ();
	block->representative = block_a.representative_field ().value ().to_account ();
	block_a.signature.encode_hex (block->signature);
	block->work = celerix::to_string_hex (block_a.work);
	return block;
}

std::unique_ptr<celerixapi::BlockChangeT> celerix::ipc::flatbuffers_builder::from (celerix::change_block const & block_a)
{
	auto block (std::make_unique<celerixapi::BlockChangeT> ());
	block->hash = block_a.hash ().to_string ();
	block->previous = block_a.previous ().to_string ();
	block->representative = block_a.representative_field ().value ().to_account ();
	block_a.signature.encode_hex (block->signature);
	block->work = celerix::to_string_hex (block_a.work);
	return block;
}

celerixapi::BlockUnion celerix::ipc::flatbuffers_builder::block_to_union (celerix::block const & block_a, celerix::amount const & amount_a, bool is_state_send_a, bool is_state_epoch_a)
{
	celerixapi::BlockUnion u;
	switch (block_a.type ())
	{
		case celerix::block_type::state:
		{
			u.Set (*from (dynamic_cast<celerix::state_block const &> (block_a), amount_a, is_state_send_a, is_state_epoch_a));
			break;
		}
		case celerix::block_type::send:
		{
			u.Set (*from (dynamic_cast<celerix::send_block const &> (block_a)));
			break;
		}
		case celerix::block_type::receive:
		{
			u.Set (*from (dynamic_cast<celerix::receive_block const &> (block_a)));
			break;
		}
		case celerix::block_type::open:
		{
			u.Set (*from (dynamic_cast<celerix::open_block const &> (block_a)));
			break;
		}
		case celerix::block_type::change:
		{
			u.Set (*from (dynamic_cast<celerix::change_block const &> (block_a)));
			break;
		}

		default:
			debug_assert (false);
	}
	return u;
}
