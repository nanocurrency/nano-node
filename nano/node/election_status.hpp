#pragma once

#include <nano/lib/numbers.hpp>

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

/* Holds a summary of an election */
class election_status final
{
public:
	std::shared_ptr<nano::block> winner;
	nano::amount tally{ 0 };
	nano::amount final_tally{ 0 };
	std::chrono::milliseconds election_end{ std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::system_clock::now ().time_since_epoch ()) };
	std::chrono::milliseconds election_duration{ std::chrono::duration_values<std::chrono::milliseconds>::zero () };
	unsigned confirmation_request_count{ 0 };
	unsigned block_count{ 0 };
	unsigned voter_count{ 0 };
	election_status_type type{ nano::election_status_type::inactive_confirmation_height };
};
}
