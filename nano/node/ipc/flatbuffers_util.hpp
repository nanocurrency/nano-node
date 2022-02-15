#pragma once

#include <nano/ipc_flatbuffers_lib/generated/flatbuffers/nanoapi_generated.h>

#include <memory>

namespace nano
{
class amount;
class block;
class send_block;
class receive_block;
class change_block;
class open_block;
class state_block;
namespace ipc
{
	/**
	 * Utilities to convert between blocks and Flatbuffers equivalents
	 */
	class flatbuffers_builder
	{
	public:
		static nanoapi::BlockUnion block_to_union (nano::block const & block_a, nano::amount const & amount_a, bool is_state_send_a = false, bool is_state_epoch_a = false);
		static std::unique_ptr<nanoapi::BlockStateT> from (nano::state_block const & block_a, nano::amount const & amount_a, bool is_state_send_a, bool is_state_epoch_a);
		static std::unique_ptr<nanoapi::BlockSendT> from (nano::send_block const & block_a);
		static std::unique_ptr<nanoapi::BlockReceiveT> from (nano::receive_block const & block_a);
		static std::unique_ptr<nanoapi::BlockOpenT> from (nano::open_block const & block_a);
		static std::unique_ptr<nanoapi::BlockChangeT> from (nano::change_block const & block_a);
	};
}
}
