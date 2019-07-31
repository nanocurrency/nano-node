#pragma once

#include <nano/node/active_transactions.hpp>
#include <nano/secure/blockstore.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <unordered_set>

namespace nano
{
class channel;
class node;
class vote_info final
{
public:
	std::chrono::steady_clock::time_point time;
	uint64_t sequence;
	nano::block_hash hash;
};
class election_vote_result final
{
public:
	election_vote_result () = default;
	election_vote_result (bool, bool);
	bool replay{ false };
	bool processed{ false };
};
class election final : public std::enable_shared_from_this<nano::election>
{
	std::function<void(std::shared_ptr<nano::block>)> confirmation_action;

public:
	election (nano::node &, std::shared_ptr<nano::block>, std::function<void(std::shared_ptr<nano::block>)> const &);
	nano::election_vote_result vote (nano::account, uint64_t, nano::block_hash);
	nano::tally_t tally (nano::transaction const &);
	// Check if we have vote quorum
	bool have_quorum (nano::tally_t const &, nano::uint128_t) const;
	// Change our winner to agree with the network
	void compute_rep_votes (nano::transaction const &);
	void confirm_once (nano::election_status_type = nano::election_status_type::active_confirmed_quorum);
	// Confirm this block if quorum is met
	void confirm_if_quorum (nano::transaction const &);
	void log_votes (nano::tally_t const &) const;
	bool publish (std::shared_ptr<nano::block> block_a);
	size_t last_votes_size ();
	void update_dependent ();
	void clear_dependent ();
	void clear_blocks ();
	void stop ();
	nano::node & node;
	std::unordered_map<nano::account, nano::vote_info> last_votes;
	std::unordered_map<nano::block_hash, std::shared_ptr<nano::block>> blocks;
	std::chrono::steady_clock::time_point election_start;
	nano::election_status status;
	std::atomic<bool> confirmed;
	bool stopped;
	std::unordered_map<nano::block_hash, nano::uint128_t> last_tally;
	unsigned confirmation_request_count;
	std::unordered_set<nano::block_hash> dependent_blocks;
};
}
