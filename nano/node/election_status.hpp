#pragma once

#include <nano/lib/fwd.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/stats_enums.hpp>
#include <nano/node/fwd.hpp>
#include <nano/secure/election_ballot.hpp>

#include <chrono>
#include <memory>
#include <unordered_map>

namespace nano
{
/* Classifies how a cemented block came to be confirmed */
enum class confirmation_type
{
	active_confirmed_quorum, // Winner of its own election reaching final quorum
	active_confirmation_height, // Cemented under a dependent election while its own election was still live
	inactive_confirmation_height, // Cemented without a live election
};

std::string_view to_string (confirmation_type);
nano::stat::detail to_stat_detail (confirmation_type);

/** Summary of an election */
struct election_status final
{
	std::shared_ptr<nano::block> winner{}; // The winning block
	nano::amount tally{ 0 }; // Vote weight behind the winner, normal + final votes
	nano::amount final_tally{ 0 }; // Vote weight behind the winner, final votes only
	std::chrono::system_clock::time_point election_end{}; // When the election confirmed, as system time
	std::chrono::milliseconds election_duration{}; // Time from election start to confirmation
	unsigned confirmation_request_count{ 0 }; // Confirmation requests sent
	unsigned vote_broadcast_count{ 0 }; // Votes broadcast for the winner
	unsigned block_count{ 0 }; // Held fork blocks
	unsigned voter_count{ 0 }; // Representatives with a recorded vote

	void operator() (nano::object_stream &) const;
};

/** Status extended with copies of the ballot state, all taken as one consistent snapshot */
struct election_extended_status final
{
	nano::election_behavior behavior; // Scheduling behavior at the time of the snapshot
	nano::election_status status; // The status summary
	std::unordered_map<nano::account, nano::vote_info> votes; // All recorded votes by representative
	std::unordered_map<nano::block_hash, std::shared_ptr<nano::block>> blocks; // All held blocks by hash
	nano::tally_map tally; // Held blocks by tally position, zero-vote blocks absent

	void operator() (nano::object_stream &) const;
};
}
