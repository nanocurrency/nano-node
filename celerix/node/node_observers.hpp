#pragma once

#include <celerix/lib/numbers.hpp>
#include <celerix/lib/utility.hpp>
#include <celerix/node/fwd.hpp>
#include <celerix/node/transport/transport.hpp>
#include <celerix/node/vote_with_weight_info.hpp>

namespace celerix
{
class node_observers final
{
public:
	using blocks_t = celerix::observer_set<celerix::election_status const &, std::vector<celerix::vote_with_weight_info> const &, celerix::account const &, celerix::uint128_t const &, bool, bool>;
	blocks_t blocks; // Notification upon election completion or cancellation
	celerix::observer_set<bool> wallet;
	celerix::observer_set<std::shared_ptr<celerix::vote>, std::shared_ptr<celerix::transport::channel>, celerix::vote_source, celerix::vote_code> vote;
	celerix::observer_set<celerix::block_hash const &> active_started;
	celerix::observer_set<celerix::block_hash const &> active_stopped;
	celerix::observer_set<celerix::account const &, bool> account_balance;
	celerix::observer_set<> disconnect;
	celerix::observer_set<celerix::root const &> work_cancel;
	celerix::observer_set<celerix::telemetry_data const &, std::shared_ptr<celerix::transport::channel> const &> telemetry;
	celerix::observer_set<std::shared_ptr<celerix::transport::tcp_socket>> socket_connected;
	celerix::observer_set<std::shared_ptr<celerix::transport::channel>> channel_connected;

	celerix::container_info container_info () const;
};
}
