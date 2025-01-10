#pragma once

#include <celerix/ipc_flatbuffers_lib/generated/flatbuffers/celerixapi_generated.h>

#include <memory>

namespace celerix
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
		static celerixapi::BlockUnion block_to_union (celerix::block const & block_a, celerix::amount const & amount_a, bool is_state_send_a = false, bool is_state_epoch_a = false);
		static std::unique_ptr<celerixapi::BlockStateT> from (celerix::state_block const & block_a, celerix::amount const & amount_a, bool is_state_send_a, bool is_state_epoch_a);
		static std::unique_ptr<celerixapi::BlockSendT> from (celerix::send_block const & block_a);
		static std::unique_ptr<celerixapi::BlockReceiveT> from (celerix::receive_block const & block_a);
		static std::unique_ptr<celerixapi::BlockOpenT> from (celerix::open_block const & block_a);
		static std::unique_ptr<celerixapi::BlockChangeT> from (celerix::change_block const & block_a);
	};
}
}
