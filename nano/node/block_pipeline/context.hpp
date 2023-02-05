#pragma once

#include <nano/secure/common.hpp>

namespace nano
{
namespace block_pipeline
{
	// Context that is passed between pipeline stages
	class context
	{
	public:
		context (std::shared_ptr<nano::block> block) :
			block{ block }
		{
		}
		context () = default;
		bool is_send () const;
		nano::account account () const;
		nano::block_hash source () const;
		nano::account const & signer (nano::epochs const & epochs) const;
		bool gap_previous () const;
		std::shared_ptr<nano::block> block;
		std::shared_ptr<nano::block> previous;
		std::optional<nano::account_info> state;
		std::optional<nano::pending_info> pending;
	private:
		static nano::account const account_one;
	};
}
}
