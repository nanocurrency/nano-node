#pragma once

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
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

	timer (nano::timer_state state_a, std::string description_a = "timer") :
	desc (description_a)
	{
		if (state_a == nano::timer_state::started)
		{
			start ();
		}
	}

	timer (std::string description_a) :
	desc (description_a)
	{
	}

	timer (std::string description_a, timer * parent_a) :
	parent (parent_a),
	desc (description_a)
	{
	}

	/** Do not output if measured time is below the time units threshold in \p minimum_a */
	timer & set_minimum (UNIT minimum_a)
	{
		minimum = minimum_a;
		return *this;
	}

	/**
	 * Create a child timer without starting it.
	 * Since the timing API needs to have low overhead, this function
	 * does not check if a timer with the same name already exists.
	 */
	timer & child (std::string description_a = "child timer")
	{
		children.emplace_back (description_a, this);
		return children.back ();
	}

	/** Create and start a child timer */
	timer & start_child (std::string description_a = "child timer")
	{
		auto & child_timer = child (description_a);
		child_timer.start ();
		return child_timer;
	}

	/** Start the timer. This will assert if the timer is already started. */
	void start ()
	{
		assert (state == nano::timer_state::stopped);
		state = nano::timer_state::started;
		begin = CLOCK::now ();
	}

	/** Restarts the timer */
	void restart ()
	{
		state = nano::timer_state::started;
		begin = CLOCK::now ();
		ticks = UNIT::zero ();
		measurements = 0;
	}

	/**
	 * Stops the timer and increases the measurement count. A timer can be started and paused
	 * multiple times (e.g. in a loop).
	 * @return duration
	 */
	UNIT pause ()
	{
		++measurements;
		return stop ();
	}

	/**
	 * Stop timer
	 * @return duration
	 */
	UNIT stop ()
	{
		assert (state == nano::timer_state::started);
		state = nano::timer_state::stopped;

		auto end = CLOCK::now ();
		ticks += std::chrono::duration_cast<UNIT> (end - begin);
		return ticks;
	}

	/**
	 * Return current units.
	 */
	UNIT value ()
	{
		return ticks;
	}

	/** Returns the duration in UNIT since the timer was last started. */
	UNIT since_start ()
	{
		auto end = CLOCK::now ();
		return std::chrono::duration_cast<UNIT> (end - begin);
	}

	/** Returns true if the timer was last started longer than \p duration_a units ago*/
	bool after_deadline (UNIT duration_a)
	{
		auto end = CLOCK::now ();
		return std::chrono::duration_cast<UNIT> (end - begin) > duration_a;
	}

	/** Returns true if the timer was last started shorter than \p duration_a units ago*/
	bool before_deadline (UNIT duration_a)
	{
		auto end = CLOCK::now ();
		return std::chrono::duration_cast<UNIT> (end - begin) < duration_a;
	}

	/** Stop timer and write measurements to \p output_a */
	void stop (std::string & output_a)
	{
		std::ostringstream stream;
		stop (stream);
		output_a = stream.str ();
	}

	/** Stop timer and write measurements to \p stream_a */
	void stop (std::ostream & stream_a)
	{
		stop ();
		print (stream_a);
	}

	/** Print measurements to the \p stream_a */
	void print (std::ostream & stream_a)
	{
		if (ticks >= minimum)
		{
			// Print cumulative children first. Non-cumulative children prints directly.
			for (auto & child : children)
			{
				if (child.measurements > 0)
				{
					child.print (stream_a);
				}
			}

			auto current_parent = parent;
			while (current_parent)
			{
				stream_a << parent->desc << ".";
				current_parent = current_parent->parent;
			}

			stream_a << desc << ": " << ticks.count () << ' ' << unit ();
			if (measurements > 0)
			{
				stream_a << " (" << measurements << " measurements, " << std::setprecision (2) << std::fixed << static_cast<double> (ticks.count ()) / static_cast<double> (measurements) << ' ' << unit () << " avg)";
			}
			stream_a << std::endl;
		}
	}

	/** Returns the SI unit string */
	std::string unit () const
	{
		return typed_unit<UNIT> ();
	}

	nano::timer_state current_state () const
	{
		return state;
	}

private:
	timer * parent{ nullptr };
	std::vector<timer> children;
	nano::timer_state state{ nano::timer_state::stopped };
	std::string desc;
	std::chrono::time_point<CLOCK> begin;
	UNIT ticks{ 0 };
	UNIT minimum{ UNIT::zero () };
	unsigned measurements{ 0 };

	template <typename U, std::enable_if_t<std::is_same<U, std::chrono::nanoseconds>::value> * = nullptr>
	std::string typed_unit () const
	{
		return "nanoseconds";
	}

	template <typename U, std::enable_if_t<std::is_same<U, std::chrono::microseconds>::value> * = nullptr>
	std::string typed_unit () const
	{
		return "microseconds";
	}

	template <typename U, std::enable_if_t<std::is_same<U, std::chrono::milliseconds>::value> * = nullptr>
	std::string typed_unit () const
	{
		return "milliseconds";
	}

	template <typename U, std::enable_if_t<std::is_same<U, std::chrono::seconds>::value> * = nullptr>
	std::string typed_unit () const
	{
		return "seconds";
	}

	template <typename U, std::enable_if_t<std::is_same<U, std::chrono::minutes>::value> * = nullptr>
	std::string typed_unit () const
	{
		return "minutes";
	}

	template <typename U, std::enable_if_t<std::is_same<U, std::chrono::hours>::value> * = nullptr>
	std::string typed_unit () const
	{
		return "hours";
	}
};

/**
 * The autotimer starts on construction, and stops and prints on destruction.
 */
template <typename UNIT = std::chrono::milliseconds>
class autotimer : public nano::timer<UNIT>
{
public:
	autotimer (std::string description_a, std::ostream & stream_a = std::cout) :
	nano::timer<UNIT> (description_a), stream (stream_a)
	{
		nano::timer<UNIT>::start ();
	}
	~autotimer ()
	{
		nano::timer<UNIT>::stop (stream);
	}

private:
	std::ostream & stream;
};
}
