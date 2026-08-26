#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/stats_enums.hpp>

#include <chrono>
#include <memory>

namespace nano
{
class block;
}

namespace nano
{
/* Defines the possible states for an election to stop in */
enum class election_status_type : uint8_t
{
	ongoing = 0,
	active_confirmed_quorum = 1,
	active_confirmation_height = 2,
	inactive_confirmation_height = 3,
	stopped = 5
};

std::string_view to_string (election_status_type);
nano::stat::detail to_stat_detail (election_status_type);

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
	election_status_type type{ nano::election_status_type::inactive_confirmation_height };
};
}
