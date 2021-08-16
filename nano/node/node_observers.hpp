#pragma once

#include "nano/node/common.hpp" // for endpoint, telemetry_data (ptr only)
#include "nano/secure/common.hpp" // for election_status (ptr only), vote (...

#include <nano/lib/numbers.hpp> // for account, uint128_t
#include <nano/lib/utility.hpp> // for observer_set

#include <memory> // for unique_ptr
#include <string> // for string
#include <vector> // for vector

#include <bits/shared_ptr.h> // for shared_ptr

namespace nano
{
class vote_with_weight_info;
}
namespace nano
{
namespace transport
{
	class channel;
}
}

namespace nano
{
class telemetry;
class node_observers final
{
public:
	using blocks_t = nano::observer_set<nano::election_status const &, std::vector<nano::vote_with_weight_info> const &, nano::account const &, nano::uint128_t const &, bool, bool>;
	blocks_t blocks;
	nano::observer_set<bool> wallet;
	nano::observer_set<std::shared_ptr<nano::vote>, std::shared_ptr<nano::transport::channel>, nano::vote_code> vote;
	nano::observer_set<nano::block_hash const &> active_stopped;
	nano::observer_set<nano::account const &, bool> account_balance;
	nano::observer_set<std::shared_ptr<nano::transport::channel>> endpoint;
	nano::observer_set<> disconnect;
	nano::observer_set<nano::root const &> work_cancel;
	nano::observer_set<nano::telemetry_data const &, nano::endpoint const &> telemetry;
};

std::unique_ptr<container_info_component> collect_container_info (node_observers & node_observers, std::string const & name);
}
