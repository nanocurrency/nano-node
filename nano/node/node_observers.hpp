#pragma once

#include <nano/lib/blocks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/active_transactions.hpp>
#include <nano/node/transport/transport.hpp>
#include <nano/secure/blockstore.hpp>

namespace nano
{
class node_observers final
{
public:
	using blocks_t = nano::observer_set<nano::election_status const &, nano::account const &, nano::uint128_t const &, bool>;
	blocks_t blocks;
	nano::observer_set<bool> wallet;
	nano::observer_set<nano::transaction const &, std::shared_ptr<nano::vote>, std::shared_ptr<nano::transport::channel>> vote;
	nano::observer_set<nano::block_hash const &> active_stopped;
	nano::observer_set<nano::account const &, bool> account_balance;
	nano::observer_set<std::shared_ptr<nano::transport::channel>> endpoint;
	nano::observer_set<> disconnect;
	nano::observer_set<uint64_t> difficulty;
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (node_observers & node_observers, const std::string & name);
}
