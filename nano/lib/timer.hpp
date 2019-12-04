#pragma once

#include <chrono>
#include <iosfwd>
#include <string>
#include <vector>

namespace nano
{
enum class timer_state
{
	started,
	stopped
};

/** Timer utility with nesting support */
template <typename UNIT = std::chrono::milliseconds, typename CLOCK = std::chrono::steady_clock>
class timer
{
public:
	timer () = default;
	timer (nano::timer_state state_a, std::string const & description_a = "timer");
	timer (std::string const & description_a);
	timer (std::string const & description_a, timer * parent_a);
	timer & set_minimum (UNIT minimum_a);
	timer & child (std::string const & description_a = "child timer");
	timer & start_child (std::string const & description_a = "child timer");
	void start ();
	void restart ();
	UNIT pause ();
	UNIT stop ();
	UNIT value () const;
	UNIT since_start () const;
	bool after_deadline (UNIT duration_a);
	bool before_deadline (UNIT duration_a);
	void stop (std::ostream & stream_a);
	void stop (std::string & output_a);
	void print (std::ostream & stream_a);
	std::string unit () const;
	nano::timer_state current_state () const;

private:
	timer * parent{ nullptr };
	std::vector<timer> children;
	nano::timer_state state{ nano::timer_state::stopped };
	std::string desc;
	std::chrono::time_point<CLOCK> begin;
	UNIT ticks{ 0 };
	UNIT minimum{ UNIT::zero () };
	unsigned measurements{ 0 };
};
}
