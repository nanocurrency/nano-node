#pragma once

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
enum class election_status_type : uint8_t
{
	ongoing = 0,
	active_confirmed_quorum = 1,
	active_confirmation_height = 2,
	inactive_confirmation_height = 3,
	stopped = 5
};
class election_status final
{
public:
	std::shared_ptr<nano::block> winner;
	nano::amount tally;
	std::chrono::milliseconds election_end;
	std::chrono::milliseconds election_duration;
	unsigned confirmation_request_count;
	unsigned block_count;
	unsigned voter_count;
	election_status_type type;
};
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
	election (nano::node &, std::shared_ptr<nano::block>, bool const, std::function<void(std::shared_ptr<nano::block>)> const &);
	nano::election_vote_result vote (nano::account, uint64_t, nano::block_hash);
	nano::tally_t tally ();
	// Check if we have vote quorum
	bool have_quorum (nano::tally_t const &, nano::uint128_t) const;
	void confirm_once (nano::election_status_type = nano::election_status_type::active_confirmed_quorum);
	// Confirm this block if quorum is met
	void confirm_if_quorum ();
	void log_votes (nano::tally_t const &) const;
	bool publish (std::shared_ptr<nano::block> block_a);
	size_t last_votes_size ();
	void update_dependent ();
	void clear_dependent ();
	void clear_blocks ();
	void insert_inactive_votes_cache (nano::block_hash const &);
	void stop ();
	nano::node & node;
	std::unordered_map<nano::account, nano::vote_info> last_votes;
	std::unordered_map<nano::block_hash, std::shared_ptr<nano::block>> blocks;
	std::chrono::steady_clock::time_point election_start;
	nano::election_status status;
	bool skip_delay;
	std::atomic<bool> confirmed;
	bool stopped;
	std::unordered_map<nano::block_hash, nano::uint128_t> last_tally;
	unsigned confirmation_request_count{ 0 };
	std::chrono::steady_clock::time_point last_broadcast;
	std::chrono::steady_clock::time_point last_request;
	std::unordered_set<nano::block_hash> dependent_blocks;
	std::chrono::seconds late_blocks_delay{ 5 };
};
}
